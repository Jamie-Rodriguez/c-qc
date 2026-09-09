# Process spawning, pipes, crash detection, timeouts, and sanitizer exit behaviour

Research for the supervisor/worker process model in
[ADR 0001](../adr/0001-supervisor-and-worker-process-model.md). The supervisor spawns a
worker from the same executable, talks to it over a pipe, classifies how it died (fatal
signal or structured exception with fault address where available, abort or failed
assert, plain exit, timeout, sanitizer report), and kills it on a deadline. Targets:
Linux, macOS, Windows (MSVC 2015+ and clang-cl), with and without ASan/UBSan/LSan.

Every claim cites a primary source (POSIX, Linux man-pages, Apple man pages and headers,
Microsoft Learn, LLVM/Clang docs and compiler-rt source, GCC docs). Items marked
**Observed** are local experiments run on 2026-09-09 on macOS 26.6.2 (arm64) with Apple
clang 21.0.0 (Xcode 26.6) and Homebrew LLVM clang 23.1.0; they supplement, and never
replace, a cited source.

Sections: [1 Spawning](#1-spawning-the-same-executable) ·
[2 Pipes](#2-pipes-and-inheritance) · [3 Death classification](#3-death-classification) ·
[4 abort and assert](#4-abort-and-assert) · [5 Timeouts](#5-timeouts-and-waiting-with-a-deadline) ·
[6 Sanitizers](#6-sanitizers) · [7 Design summary](#7-summary-of-what-this-means-for-qc)

---

## 1. Spawning the same executable

### Linux

**`posix_spawn` vs `fork`+`exec`.** POSIX: "The posix_spawn() and posix_spawnp() functions
shall create a new process (child process) from the specified process image." On success
the child's PID is stored via the `pid` argument and 0 is returned; on failure "no child
process shall be created ... and an error number shall be returned as the function return
value" ([POSIX posix_spawn](https://pubs.opengroup.org/onlinepubs/9699919799/functions/posix_spawn.html)).
glibc implements it efficiently: "Since glibc 2.24, the posix_spawn() function commences
by calling clone(2) with CLONE_VM and CLONE_VFORK flags"
([posix_spawn(3)](https://man7.org/linux/man-pages/man3/posix_spawn.3.html)).
File actions are applied "in the order that they were specified using calls to the
posix_spawn_file_actions_add*() functions", and if the exec fails "the child process will
exit with the exit value of 127" — the parent's `posix_spawn` call still returns 0 in that
case, so a 127 exit must be treated as a spawn failure by the supervisor
([posix_spawn(3)](https://man7.org/linux/man-pages/man3/posix_spawn.3.html)).

**Path of the running executable.** `/proc/self/exe` (i.e. `/proc/[pid]/exe` for the
calling process) is "a symbolic link containing the actual pathname of the executed
command"; read it with `readlink(2)`. Caveats: if the binary was deleted the link text has
" (deleted)" appended; reading it is "governed by a ptrace access mode
PTRACE_MODE_READ_FSCREDS check"; and it is unavailable "if the main thread has already
terminated (typically by calling pthread_exit(3))"
([proc_pid_exe(5)](https://man7.org/linux/man-pages/man5/proc_pid_exe.5.html)).
Because the link can also be *executed* directly, `posix_spawn("/proc/self/exe", ...)`
re-executes the same binary without resolving the path at all
([proc_pid_exe(5)](https://man7.org/linux/man-pages/man5/proc_pid_exe.5.html)).

### macOS

**Fork is hazardous; use `posix_spawn`.** Apple's `fork(2)` man page, CAVEATS: "There are
limits to what you can do in the child process. To be totally safe you should restrict
yourself to only executing async-signal safe operations until such time as one of the
exec functions is called. All APIs, including global data symbols, in any framework or
library should be assumed to be unsafe after a fork() unless explicitly documented to be
safe or async-signal safe. If you need to use these frameworks in the child process, you
must exec. In this situation it is reasonable to exec yourself." (macOS `fork(2)` man
page, Xcode 26.6 Command Line Tools; the archived copy at
developer.apple.com returns 404 as of this writing.) Apple DTS (Quinn "The Eskimo!")
adds: "If you program in C or C++ and limit yourself to Posix APIs then fork without
exec* should work reliably", recommends `posix_spawn` as the "hot path", and measured
"fork is not faster than posix_spawn, each took about 120 µs"
([Apple Developer Forums thread 747499](https://developer.apple.com/forums/thread/747499)).
Practical consequence: a plain C99 library may fork, but any user test code that touches
Foundation/CoreFoundation/Objective-C in the worker would break; `posix_spawn` avoids the
question entirely.

**Apple-specific spawn attributes.** Apple's `posix_spawnattr_setflags(3)` documents
`POSIX_SPAWN_SETEXEC` ("posix_spawn(2) and posix_spawnp(2) will behave as a more
featureful execve(2)"), `POSIX_SPAWN_START_SUSPENDED`, and `POSIX_SPAWN_CLOEXEC_DEFAULT`
("only file descriptors explicitly created by the file_actions argument are available in
the spawned process; all of the other file descriptors are automatically closed in the
spawned process") (macOS `posix_spawnattr_setflags(3)` man page, Xcode 26.6). The XNU
header defines the values: `POSIX_SPAWN_SETEXEC 0x0040`, `POSIX_SPAWN_START_SUSPENDED 0x0080`,
`POSIX_SPAWN_SETSID 0x0400`, `POSIX_SPAWN_CLOEXEC_DEFAULT 0x4000`
([xnu bsd/sys/spawn.h](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/sys/spawn.h)).
Apple's `posix_spawn(2)` otherwise follows POSIX: "File descriptors open in the calling
process image remain open in the new process image, except for those for which the
close-on-exec flag is set"
([Apple posix_spawn(2)](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/posix_spawn.2.html)).

**Path of the running executable.** `int _NSGetExecutablePath(char* buf, uint32_t* bufsize)`
"Copies the path of the main executable into the buffer buf"; returns 0 on success, or
-1 "if the buffer is not large enough, and *bufsize is set to the size required". It
returns "a path" not a "real path" ("the path may be a symbolic link and not the real
file"), so call `realpath(3)` if canonical form matters, and "With deep directories the
total bufsize needed could be more than MAXPATHLEN"
([Apple dyld(3)](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/dyld.3.html)).

### Windows

**`CreateProcessW`.** "Creates a new process and its primary thread." Key rules:
`lpCommandLine` "can modify the contents of this string. Therefore, this parameter cannot
be a pointer to read-only memory (such as a const variable or a literal string)"; pass a
non-NULL `lpApplicationName` (full path, extension included; "The function will not use
the search path") to avoid the documented space-in-path ambiguity where
`C:\Program Files\MyApp` may run `C:\Program.exe`; `bInheritHandles` "If this parameter
is TRUE, each inheritable handle in the calling process is inherited by the new process";
"Handles in PROCESS_INFORMATION must be closed with CloseHandle when they are no longer
needed"; and "the function returns before the process has finished initialization. If a
required DLL cannot be located or fails to initialize, the process is terminated. To get
the termination status of a process, call GetExitCodeProcess"
([CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw)).
Useful creation flags: `CREATE_NO_WINDOW` ("a console application that is being run
without a console window"), `CREATE_DEFAULT_ERROR_MODE` ("The new process does not inherit
the error mode of the calling process" — the default is to inherit, see §3),
`CREATE_NEW_PROCESS_GROUP` (disables CTRL+C in the child), `CREATE_SUSPENDED`, and
`EXTENDED_STARTUPINFO_PRESENT` for `STARTUPINFOEX`
([Process Creation Flags](https://learn.microsoft.com/en-us/windows/win32/procthread/process-creation-flags)).

**Path of the running executable.** `GetModuleFileNameW(NULL, buf, nSize)`: "If this
parameter is NULL, GetModuleFileName retrieves the path of the executable file of the
current process." If the buffer is too small "the string is truncated to nSize characters
including the terminating null character, the function returns nSize, and the function
sets the last error to ERROR_INSUFFICIENT_BUFFER" (Windows XP: not null-terminated and
last error not modified). The result "can use the prefix \\?" and may be a long or short
name
([GetModuleFileNameW](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamew)).
Loop: grow the buffer until the return value is less than `nSize`.

---

## 2. Pipes and inheritance

### Linux

`pipe(2)`: "pipefd[0] refers to the read end of the pipe. pipefd[1] refers to the write
end of the pipe." `pipe2(2)` accepts `O_CLOEXEC` ("Set the close-on-exec (FD_CLOEXEC)
flag on the two new file descriptors") and `O_NONBLOCK`; `pipe2` is "POSIX.1-2024, Linux
2.6.27, glibc 2.9"
([pipe(2)](https://man7.org/linux/man-pages/man2/pipe.2.html)). Creating both ends
`O_CLOEXEC` and then using spawn file actions is the race-free pattern: with a non-NULL
`file_actions`, the child's descriptors are "those open in the calling process as modified
by the spawn file actions object", after which "Any file descriptor that has its
FD_CLOEXEC flag set shall be closed"
([POSIX posix_spawn](https://pubs.opengroup.org/onlinepubs/9699919799/functions/posix_spawn.html)).
`posix_spawn_file_actions_adddup2(fa, fildes, newfildes)` adds an action that "shall cause
the file descriptor fildes to be duplicated as newfildes (as if dup2(fildes, newfildes)
had been called)"; `dup2` produces a descriptor without `FD_CLOEXEC`, so the duplicate
survives the exec while the original is closed
([POSIX posix_spawn_file_actions_adddup2](https://pubs.opengroup.org/onlinepubs/9699919799/functions/posix_spawn_file_actions_adddup2.html)).
Recommended layout: dup the worker's read end onto a fixed fd (or pass the fd number on
the command line) and let `FD_CLOEXEC` close the supervisor's ends in the child.

### macOS

Same POSIX API. `pipe2` is not part of POSIX.1-2008 (it is POSIX.1-2024 per the Linux man
page above) and Apple's `pipe(2)` does not provide it, so set `FD_CLOEXEC` with
`fcntl(fd, F_SETFD, FD_CLOEXEC)` after `pipe()`. The Apple-only
`POSIX_SPAWN_CLOEXEC_DEFAULT` attribute closes *everything* not named in file actions
(macOS `posix_spawnattr_setflags(3)`; value in
[xnu spawn.h](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/sys/spawn.h)),
which makes the child's descriptor table deterministic regardless of what the host test
binary had open; on Linux the equivalent is enumerating `/proc/self/fd` or relying on
`FD_CLOEXEC` discipline.

### Windows

`CreatePipe(&rd, &wr, &sa, 0)` "Creates an anonymous pipe"; `lpPipeAttributes`
"determines whether the returned handle can be inherited by child processes. If
lpPipeAttributes is NULL, the handle cannot be inherited"
([CreatePipe](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-createpipe)).
Microsoft's canonical recipe: set `SECURITY_ATTRIBUTES.bInheritHandle = TRUE` so both
ends are inheritable, then "before creating the child process, the parent process uses
the SetHandleInformation function to ensure that the write handle for the child process's
standard input and the read handle for the child process's standard output cannot be
inherited"; put the child's ends in `STARTUPINFO.hStdInput/hStdOutput/hStdError` with
`dwFlags |= STARTF_USESTDHANDLES`; call `CreateProcess(..., bInheritHandles=TRUE, ...)`;
then in the parent "Close handles to the stdin and stdout pipes no longer needed by the
child process. If they are not explicitly closed, there is no way to recognize that the
child process has ended"
([Creating a Child Process with Redirected Input and Output](https://learn.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output)).
`SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0)` clears inheritance: "If this flag is
set, a child process created with the bInheritHandles parameter of CreateProcess set to
TRUE will inherit the object handle"
([SetHandleInformation](https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-sethandleinformation)).
`CreateProcessW` warns that "the standard handle fields in STARTUPINFO ... are copied
unchanged to the child process without validation, even when the dwFlags member specifies
STARTF_USESTDHANDLES"
([CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw)).

*Leaking other inheritable handles.* `bInheritHandles=TRUE` inherits *every* inheritable
handle in the process, which "can be problematic for applications which create processes
from multiple threads simultaneously"; the fix is `UpdateProcThreadAttribute` with
`PROC_THREAD_ATTRIBUTE_HANDLE_LIST` ("a pointer to a list of handles to be inherited by
the child process. These handles must be created as inheritable handles ... pass in a
value of TRUE for the bInheritHandles parameter"), which requires `STARTUPINFOEX` and
`EXTENDED_STARTUPINFO_PRESENT` (Windows Vista+)
([CreateProcessW Remarks](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw),
[UpdateProcThreadAttribute](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute)).

*Blocking semantics.* "Asynchronous (overlapped) read and write operations are not
supported by anonymous pipes ... the lpOverlapped parameter of ReadFile and WriteFile is
ignored". `ReadFile` "returns when another process has written to the pipe" or "if all
write handles to the pipe have been closed"; `WriteFile` blocks when the buffer is full
([Anonymous Pipe Operations](https://learn.microsoft.com/en-us/windows/win32/ipc/anonymous-pipe-operations)).
This is why the timeout design in §5 waits on the *process handle* rather than the pipe,
and polls the pipe with `PeekNamedPipe` (see §5).

---

## 3. Death classification

### Linux (and macOS: identical POSIX semantics)

**What `waitpid` tells the supervisor.** `WIFEXITED(status)` "Evaluates to a non-zero
value if status was returned for a child process that terminated normally";
`WEXITSTATUS` gives "the low-order 8 bits of the status argument that the child process
passed to _exit() or exit()"; `WIFSIGNALED` "Evaluates to a non-zero value if status was
returned for a child process that terminated due to the receipt of a signal that was not
caught"; `WTERMSIG` "evaluates to the number of the signal that caused the termination of
the child process". With `WNOHANG`, waitpid "shall not suspend execution of the calling
thread if status is not immediately available" and returns 0 in that case
([POSIX waitpid](https://pubs.opengroup.org/onlinepubs/9699919799/functions/waitpid.html)).
The status word carries the *signal number only*; there is no field for a fault address.
**The fault address is not recoverable by the parent from `waitpid`.**

**Default actions.** SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV, SIGTRAP and SIGKILL all have
default action "A" (abnormal termination with additional actions, i.e. possible core dump)
([POSIX signal.h](https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/signal.h.html)).
Shells report signal deaths as "greater than 128" (the familiar 139 = 128+SIGSEGV,
134 = 128+SIGABRT), but that is a shell convention, not what `waitpid` returns
([POSIX sh 2.8.2](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html#tag_18_08_02)).

**Getting the fault address: the worker must catch the signal.** Install a handler with
`sigaction` and `SA_SIGINFO`: "If SA_SIGINFO is set and the signal is caught, the
signal-catching function shall be entered as: void func(int signo, siginfo_t *info, void
*context)"
([POSIX sigaction](https://pubs.opengroup.org/onlinepubs/9699919799/functions/sigaction.html)).
`siginfo_t` contains `void *si_addr`; for SIGILL and SIGFPE it is the "Address of faulting
instruction", for SIGSEGV and SIGBUS the address of the faulting memory reference, and
`si_code` distinguishes `SEGV_MAPERR` ("Address not mapped to object") from `SEGV_ACCERR`
("Invalid permissions for mapped object") and `BUS_ADRALN`/`BUS_ADRERR`/`BUS_OBJERR`
([POSIX signal.h](https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/signal.h.html)).
Linux confirms: "SIGILL, SIGFPE, SIGSEGV, SIGBUS, and SIGTRAP fill in si_addr with the
address of the fault"
([sigaction(2)](https://man7.org/linux/man-pages/man2/sigaction.2.html)).

**Re-raising so the supervisor still sees `WTERMSIG`.** Returning from the handler is not
an option: "the behavior of a process is undefined after it ignores a SIGFPE, SIGILL, or
SIGSEGV signal that was not generated by kill(2) or raise(3)"
([sigaction(2)](https://man7.org/linux/man-pages/man2/sigaction.2.html)). Instead the
handler writes its report and then terminates with the same signal. Two flags make this
clean: `SA_RESETHAND` — "the disposition of the signal shall be reset to SIG_DFL and the
SA_SIGINFO flag shall be cleared on entry to the signal handler" (note: "SIGILL and SIGTRAP
cannot be automatically reset when delivered") — and `SA_NODEFER` — the signal is not
added to the mask during the handler
([POSIX sigaction](https://pubs.opengroup.org/onlinepubs/9699919799/functions/sigaction.html)).
With `SA_RESETHAND` set, `raise(signo)` inside the handler delivers the signal with the
default action (immediately with `SA_NODEFER`, otherwise as soon as the handler returns
and the signal is unblocked), so the supervisor observes `WIFSIGNALED && WTERMSIG == signo`.
`raise`, `kill`, `sigaction`, `signal`, `_exit` and `abort` are all async-signal-safe (next
paragraph).

**What the handler may do.** POSIX: "the behavior is undefined if the signal handler
refers to any object other than errno with static storage duration other than by
assigning a value to an object declared as volatile sig_atomic_t, or if the signal handler
calls any function defined in this standard other than one of the functions listed in the
following table." The table includes `_exit`, `_Exit`, `abort`, `close`, `dup2`, `getpid`,
`kill`, `raise`, `sigaction`, `signal`, `waitpid`, `write` (and `pthread_sigmask`,
`sleep`)
([POSIX XSH 2.4.3 Signal Actions](https://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html#tag_15_04_03)).
Linux's `signal-safety(7)` lists the same set and adds that "Fetching and setting the
value of errno is async-signal-safe provided that the signal handler saves errno on entry
and restores its value before returning"
([signal-safety(7)](https://man7.org/linux/man-pages/man7/signal-safety.7.html)).
So the handler must: format the report into a pre-allocated static buffer with hand-rolled
integer/hex formatting (no `snprintf`, no `malloc`, no stdio), `write(2)` it to the pipe
in a loop handling short writes and `EINTR`, then `raise`. Keep the payload small enough
to fit the pipe's atomic write size so a partially written record cannot interleave with
anything else.

**Stack overflow.** A SIGSEGV caused by stack exhaustion cannot run a handler on the
exhausted stack. `sigaltstack` "allows a process to define and examine the state of an
alternate stack for signal handlers for the current thread"; use `SA_ONSTACK` ("the signal
shall be delivered to the calling process on that stack") and size it with `SIGSTKSZ`
(`MINSIGSTKSZ` is the minimum)
([POSIX sigaltstack](https://pubs.opengroup.org/onlinepubs/9699919799/functions/sigaltstack.html),
[POSIX sigaction](https://pubs.opengroup.org/onlinepubs/9699919799/functions/sigaction.html)).
Sanitizer runtimes do this themselves (`use_sigaltstack=true` by default, §6).

**Plain exit.** `WIFEXITED` with `WEXITSTATUS` in 0–255. Reserve a value (127 is already
taken by a failed exec, see §1) for protocol errors.

### macOS specifics

Identical POSIX semantics and macros. `si_addr` and `si_code` are the same
([POSIX signal.h](https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/signal.h.html)).
Two differences matter in practice: (1) unaligned or unmapped accesses on macOS/arm64
often arrive as SIGBUS rather than SIGSEGV, so the handler must cover SIGSEGV, SIGBUS,
SIGILL, SIGFPE and SIGTRAP (all documented to fill `si_addr`, above); (2) under ASan on
Apple platforms `abort_on_error` defaults to *true*, so a sanitized segfault reaches the
supervisor as SIGABRT, not SIGSEGV (§6, with observation).

### Windows

**Exit code doubles as the exception code.** `GetExitCodeProcess` "returns immediately";
while the process runs the status is `STILL_ACTIVE` (259); after termination it is one
of: "The exit value specified in the ExitProcess or TerminateProcess function", "The
return value from the main or WinMain function", or "The exception value for an unhandled
exception that caused the process to terminate"
([GetExitCodeProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodeprocess)).
Exception values are NTSTATUS codes: `STATUS_ACCESS_VIOLATION 0xC0000005`,
`STATUS_IN_PAGE_ERROR 0xC0000006`, `STATUS_ILLEGAL_INSTRUCTION 0xC000001D`,
`STATUS_FLOAT_DIVIDE_BY_ZERO 0xC000008E`, `STATUS_INTEGER_DIVIDE_BY_ZERO 0xC0000094`,
`STATUS_PRIVILEGED_INSTRUCTION 0xC0000096`, `STATUS_STACK_OVERFLOW 0xC00000FD`,
`STATUS_CONTROL_C_EXIT 0xC000013A`, `STATUS_DATATYPE_MISALIGNMENT 0x80000002`,
`STATUS_BREAKPOINT 0x80000003`
([MS-ERREF NTSTATUS values](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/596a1078-e883-4972-9bbc-49e60bebca55)).
Heuristic: an exit code with the severity bits `0xC0000000` set (or `0x80000000` for the
warning-class codes above) is a crash; anything else is a plain exit. A program can of
course call `exit(0xC0000005)`, so prefer the worker's own report (below) and treat the
exit code as the fallback.

**Getting the fault address: a vectored exception handler in the worker.** "Vectored
handlers are not frame-based ... Vectored handlers are called in the order that they were
added, after the debugger gets a first chance notification, but before the system begins
unwinding the stack"
([Vectored Exception Handling](https://learn.microsoft.com/en-us/windows/win32/debug/vectored-exception-handling)).
`AddVectoredExceptionHandler(First, Handler)`: "If the parameter is nonzero, the handler
is the first handler to be called"
([AddVectoredExceptionHandler](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-addvectoredexceptionhandler)).
The handler receives `EXCEPTION_POINTERS` whose `EXCEPTION_RECORD` has `ExceptionCode`,
`ExceptionAddress` ("The address where the exception occurred"), and for
`EXCEPTION_ACCESS_VIOLATION` / `EXCEPTION_IN_PAGE_ERROR`: "The first element of the array
contains a read-write flag ... If this value is zero, the thread attempted to read the
inaccessible data. If this value is 1, the thread attempted to write to an inaccessible
address. If this value is 8, the thread caused a user-mode data execution prevention (DEP)
violation. The second array element specifies the virtual address of the inaccessible
data" (and for in-page errors "The third array element specifies the underlying NTSTATUS
code")
([EXCEPTION_RECORD](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-exception_record)).
The worker's VEH writes `{code, ExceptionAddress, ExceptionInformation[0..1]}` to the pipe
and returns `EXCEPTION_CONTINUE_SEARCH` so normal termination proceeds and the exit code
still equals the exception code. Because the VEH runs before SEH unwinding, it also sees
exceptions that a user `__try/__except` in the property would later swallow; filter on
the fatal codes only.

**Top-level filter and the "Application Error" dialog.** `SetUnhandledExceptionFilter`
installs a filter called "whenever the UnhandledExceptionFilter function gets control, and
the process is not being debugged"; returning `EXCEPTION_EXECUTE_HANDLER` "usually results
in process termination", `EXCEPTION_CONTINUE_SEARCH` means "obeying the SetErrorMode flags,
or invoking the Application Error pop-up message box"
([SetUnhandledExceptionFilter](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-setunhandledexceptionfilter)).
`UnhandledExceptionFilter` itself: "If the process is not being debugged, the function
displays an Application Error message box, depending on the current error mode. The
default behavior is to display the dialog box, but this can be disabled by specifying
SEM_NOGPFAULTERRORBOX in a call to the SetErrorMode function"
([UnhandledExceptionFilter](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-unhandledexceptionfilter)).
Note that the ASan runtime intercepts `SetUnhandledExceptionFilter` (§6), so a VEH is the
more robust place for the worker's reporter.

**Suppressing dialogs so a crashed worker cannot hang.** `SetErrorMode`:
`SEM_FAILCRITICALERRORS` ("Best practice is that all applications call the process-wide
SetErrorMode function with a parameter of SEM_FAILCRITICALERRORS at startup. This is to
prevent error mode dialogs from hanging the application"), `SEM_NOGPFAULTERRORBOX` ("The
system does not invoke Windows Error Reporting. To disable Windows Error Reporting UI,
call WerSetFlags with the WER_FAULT_REPORTING_NO_UI flag"), `SEM_NOOPENFILEERRORBOX`.
"A child process inherits the error mode of its parent process", so the supervisor can
set it once before `CreateProcess` (unless `CREATE_DEFAULT_ERROR_MODE` is passed); the
worker should set it again defensively
([SetErrorMode](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-seterrormode),
[Process Creation Flags](https://learn.microsoft.com/en-us/windows/win32/procthread/process-creation-flags)).
`WerSetFlags` "Sets the Windows Error Reporting (WER) settings for the current process"
(the page documents `WER_FAULT_REPORTING_FLAG_NOHEAP`, `_QUEUE`, `_QUEUE_UPLOAD`,
`_DISABLE_THREAD_SUSPENSION`, `ALWAYS_SHOW_UI`; `WER_FAULT_REPORTING_NO_UI` is the flag
`SetErrorMode` refers to and is defined in `werapi.h`)
([WerSetFlags](https://learn.microsoft.com/en-us/windows/win32/api/werapi/nf-werapi-wersetflags)).
A belt-and-braces alternative is a job object with
`JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION`: "Forces a call to the SetErrorMode function
with the SEM_NOGPFAULTERRORBOX flag for each process associated with the job ... If there
is no debugger, the functions returns EXCEPTION_EXECUTE_HANDLER. Normally, this will cause
termination of the process with the exception code as the exit status"
([JOBOBJECT_BASIC_LIMIT_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_limit_information)).
CRT-level dialogs (assert, abort) are covered in §4.

**Stack overflow.** `SetThreadStackGuarantee` "Sets the minimum size of the stack
associated with the calling thread ... that will be available during any stack overflow
exceptions"; call it in the worker at startup so the VEH has room to run on
`EXCEPTION_STACK_OVERFLOW`
([SetThreadStackGuarantee](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadstackguarantee)).

**`signal(SIGSEGV)` on Windows is not the mechanism.** The MSVC CRT's `signal` accepts
SIGSEGV, but a CRT signal handler gets only the signal number (no address), "By default,
signal terminates the calling program with exit code 3, regardless of the value of sig",
and the handler restrictions forbid I/O and system calls
([MSVC signal](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/signal)).
Use the VEH.

---

## 4. abort and assert

### Linux and macOS

`abort()` "shall cause abnormal process termination to occur, unless the signal SIGABRT is
being caught and the signal handler does not return"; it overrides blocking/ignoring of
SIGABRT; and "The status made available to wait(), waitid(), or waitpid() by abort() shall
be that of a process terminated by the SIGABRT signal"
([POSIX abort](https://pubs.opengroup.org/onlinepubs/9699919799/functions/abort.html)).
So the supervisor sees `WIFSIGNALED && WTERMSIG == SIGABRT`. A failed `assert()` "shall
write information about the particular call that failed on stderr" (expression text,
file, line, function) "and shall call abort()"; defining `NDEBUG` before `<assert.h>`
disables it
([POSIX assert](https://pubs.opengroup.org/onlinepubs/9699919799/functions/assert.html)).
The assert message goes to the worker's stderr, so the supervisor should either inherit
stderr or capture it via a second pipe if it wants the text. Because SIGABRT is catchable,
the worker can install the same `SA_SIGINFO` handler on SIGABRT to emit a structured
"abort" record before re-raising; `si_addr` is meaningless there.

### Windows (MSVC CRT, also used by clang-cl)

**Exit code 3.** "If the Windows error reporting handler isn't invoked, then abort calls
_exit to terminate the process with exit code 3 and returns control to the parent process
or the operating system. _exit doesn't flush stream buffers or do atexit/_onexit
processing"
([abort](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/abort)).
Two flags govern the rest: `_WRITE_ABORT_MSG` ("determines whether a helpful text message
is printed when a program is abnormally terminated ... The default behavior is to print
the message") and `_CALL_REPORTFAULT` ("if set, invokes the Windows Error Reporting Service
mechanism ... By default, crash dump reporting is enabled in non-DEBUG builds")
([_set_abort_behavior](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/set-abort-behavior)).
With the debug runtime, "the abort routine displays an error message before SIGABRT is
raised. For console apps running in console mode, the message is sent to STDERR. Windows
desktop apps and console apps running in windowed mode display the message in a message
box"; in debug mode that box has Abort/Retry/Ignore. With `_CALL_REPORTFAULT` in retail
builds "Windows displays a message box that has text something like 'A problem caused the
program to stop working correctly'" and the user closes it "to terminate the app with an
error code that's defined by the operating system"
([abort](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/abort)).
Therefore the worker must call
`_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT)` at startup so `abort()`
deterministically exits with code 3 and never shows UI. `abort` "checks whether an abort
signal handler is set. If a non-default signal handler is set, abort calls raise(SIGABRT)"
— so a `signal(SIGABRT, ...)` handler in the worker can write an "abort" record to the
pipe before returning (after which the CRT proceeds to `_exit(3)`)
([abort](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/abort)).

**`assert` in debug vs release.** "The assert macro is enabled in both the release and
debug versions of the C run-time libraries when NDEBUG isn't defined." On failure it
"prints a diagnostic message ... and calls abort". Destination: "Console applications
receive the message through stderr. In a Windows-based application, assert calls the
Windows MessageBox function to create a message box ... with three buttons: Abort, Retry,
and Ignore." Either way, "a dialog box is always displayed following an assert call in
debug mode" unless suppressed, because `abort` shows its own box when `assert` went to
stderr. `_set_error_mode(_OUT_TO_STDERR)` forces the stderr path
([assert](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/assert-macro-assert-wassert),
[_set_error_mode](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/set-error-mode)).
The Microsoft-specific `_ASSERT`/`_ASSERTE` macros and other `_CrtDbgReport` users are
debug-CRT only and default to "a debug message window"; redirect with
`_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE)` plus
`_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR)` (also `_CRT_ERROR`, `_CRT_WARN`);
"When _DEBUG isn't defined, calls to _CrtSetReportMode are removed during preprocessing"
([_CrtSetReportMode](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/crtsetreportmode)).

**Worker startup checklist on Windows:** `SetErrorMode(SEM_FAILCRITICALERRORS |
SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX)`; `_set_abort_behavior(0,
_WRITE_ABORT_MSG | _CALL_REPORTFAULT)`; `_set_error_mode(_OUT_TO_STDERR)`; under `_DEBUG`,
`_CrtSetReportMode`/`_CrtSetReportFile` for `_CRT_ASSERT` and `_CRT_ERROR`;
`SetThreadStackGuarantee`; `AddVectoredExceptionHandler(1, ...)`. Classification: exit
code 3 = abort/assert (ambiguous with a user `exit(3)`, hence the pipe record), NTSTATUS
crash codes = fault, our own `TerminateProcess` code = timeout, anything else = plain
exit.

---

## 5. Timeouts and waiting with a deadline

### Linux

**Killing.** `kill(pid, SIGKILL)`: "The kill() function shall send a signal to a process
or a group of processes specified by pid"; `sig == 0` performs error checking only ("can
be used to check the validity of pid"); `ESRCH` when no such process
([POSIX kill](https://pubs.opengroup.org/onlinepubs/9699919799/functions/kill.html)).
SIGKILL cannot be caught, so the worker dies with `WTERMSIG == SIGKILL`; the supervisor
records "timeout" itself since it sent the signal.

**Waiting with a deadline, option A — `poll` on the pipe.** `poll` takes a millisecond
timeout ("A value of 0 indicates that the call timed out"); `POLLIN` means "Data other
than high-priority data may be read without blocking"; `POLLHUP` means "a pipe or FIFO has
been closed by the last process that had it open for writing" and is always reported in
`revents`
([POSIX poll](https://pubs.opengroup.org/onlinepubs/9699919799/functions/poll.html)).
Because the worker's write end is closed by the kernel when the worker dies (however it
dies), `POLLHUP` on the supervisor's read end is a portable death notification, after
which `waitpid(pid, &st, 0)` collects the status. `poll` returning 0 is the timeout.
Portable to macOS as-is.

**Option B — `pidfd`.** `pidfd_open(pid, flags)` (Linux 5.3) "creates a file descriptor
that refers to the task referenced by pid"; with poll/select/epoll it "becomes readable via
EPOLLIN when the process terminates and becomes a zombie"; it "can be waited on using
waitid(2)" with `P_PIDFD`, and `pidfd_send_signal` signals it without touching the PID.
`PIDFD_NONBLOCK` is Linux 5.10. The documented PID-reuse race applies to opening an
arbitrary PID; for the supervisor's own *unreaped* child the PID cannot be recycled, so
`posix_spawn` followed by `pidfd_open` is race-free
([pidfd_open(2)](https://man7.org/linux/man-pages/man2/pidfd_open.2.html)).
A single `poll` over `{pipe, pidfd}` then covers both data and death.

**Option C — `waitpid(WNOHANG)` polling loop** (returns 0 while the child runs, see §3):
simplest, but it needs a sleep between probes and cannot wake on pipe data; use A or B.

**Orphan protection.** If the supervisor is killed, the worker should die too:
`prctl(PR_SET_PDEATHSIG, SIGKILL)` in the worker sets "the signal that the calling process
will get when its parent dies"; note "The 'parent' in this case is considered to be the
thread that created this process", and the setting "is cleared for the child of a fork(2)"
so it must be set by the worker itself after exec
([PR_SET_PDEATHSIG](https://man7.org/linux/man-pages/man2/pr_set_pdeathsig.2const.html)).

### macOS

`kill`, `poll` on the pipe, and `waitpid` behave as above (POSIX). There is no `pidfd`;
the native equivalent is `kqueue` with `EVFILT_PROC`: "Takes the process ID to monitor as
the identifier and the events to watch for in fflags", with `NOTE_EXIT` ("The process has
exited") and, on macOS, `NOTE_EXITSTATUS` ("The process has exited and its exit status is
in filter specific data. Valid only on child processes and to be used along with
NOTE_EXIT"); `NOTE_REAP` is "Deprecated, use NOTE_EXIT"
([Apple kqueue(2)](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/kqueue.2.html);
`NOTE_EXITSTATUS` and the deprecation note are from the macOS `kqueue(2)` man page,
Xcode 26.6). The same kqueue can carry `EVFILT_READ` on the pipe: "Returns when the there
is data to read; data contains the number of bytes available. When the last writer
disconnects, the filter will set EV_EOF in flags". `kevent`'s timeout is a `struct
timespec`; NULL "waits indefinitely", a zero-valued timespec polls
([Apple kqueue(2)](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/kqueue.2.html)).
Given that `poll`+`POLLHUP` already works on both Unix platforms, kqueue is an
optimisation, not a requirement. macOS has no `PR_SET_PDEATHSIG`; a worker can instead
detect supervisor death by `POLLHUP`/`EV_EOF` on its command pipe.

### Windows

**Waiting.** `WaitForSingleObject(hProcess, ms)` "Waits until the specified object is in
the signaled state or the time-out interval elapses"; a process handle is a valid wait
object; returns `WAIT_OBJECT_0` (signaled, i.e. the process terminated), `WAIT_TIMEOUT`
("The time-out interval elapsed"), or `WAIT_FAILED`; `INFINITE` waits forever; the handle
needs `SYNCHRONIZE` access (which `CreateProcess`'s returned handle has)
([WaitForSingleObject](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject)).
Since anonymous pipes cannot be overlapped (§2), a supervisor that wants "wake on data or
death within a deadline" has three documented options: (a) a reader thread that blocks in
`ReadFile` and signals an event, with the main thread in `WaitForMultipleObjects({event,
hProcess}, timeout)`; (b) poll `PeekNamedPipe(hRead, NULL, 0, NULL, &avail, NULL)` — valid
for "the read end of an anonymous pipe, as returned by the CreatePipe function", "The
function always returns immediately in a single-threaded application, even if there is no
data in the pipe" — between short `WaitForSingleObject(hProcess, slice)` calls
([PeekNamedPipe](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-peeknamedpipe));
or (c) replace the anonymous pipe with a named pipe opened `FILE_FLAG_OVERLAPPED` so the
read's event handle can join the wait. Option (b) is the least machinery; (c) is the
closest analogue of Unix `poll`.

**Killing.** `TerminateProcess(hProcess, uExitCode)` "Terminates the specified process
and all of its threads"; the exit code "to be used by the process and threads terminated as
a result of this call. Use the GetExitCodeProcess function to retrieve a process's exit
value"; it "is asynchronous; it initiates termination and returns immediately. If you
need to be sure the process has terminated, call the WaitForSingleObject function with a
handle to the process"; the handle needs `PROCESS_TERMINATE`
([TerminateProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess)).
Pick a distinctive `uExitCode` (e.g. a private value in the user-defined range) so the
timeout is self-identifying even if the pipe record is lost.

**Orphan protection.** Put the worker in a job object with
`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`: "Causes all processes associated with the job to
terminate when the last handle to the job is closed" (requires
`JOBOBJECT_EXTENDED_LIMIT_INFORMATION`); the same job can carry
`JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION` from §3
([JOBOBJECT_BASIC_LIMIT_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_limit_information)).

---

## 6. Sanitizers

### 6.1 How ASan/UBSan/LSan exit on a report (all platforms, Clang/GCC runtimes)

**The death path.** compiler-rt's `Die()` runs the user death callback, the internal die
callbacks, then `if (common_flags()->abort_on_error) Abort(); internal__exit(common_flags()->exitcode);`
([sanitizer_termination.cpp](https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/sanitizer_common/sanitizer_termination.cpp)).
The relevant common flags (shared by ASan, UBSan, LSan, TSan, MSan) are:

| flag | default | source text |
|---|---|---|
| `exitcode` | 1 | "Override the program exit status if the tool found an error" |
| `abort_on_error` | `SANITIZER_ANDROID \|\| SANITIZER_APPLE` (false on Linux/Windows, **true on macOS**) | "If set, the tool calls abort() instead of _exit() after printing the error report." |
| `handle_segv` / `handle_sigbus` / `handle_sigfpe` | `kHandleSignalYes` (1) | "Controls custom tool's SEGV handler (0 - do not registers the handler, 1 - register the handler and allow user to set own, 2 - registers the handler and block user from changing it). Ignored on Windows." |
| `handle_abort` / `handle_sigill` / `handle_sigtrap` | `kHandleSignalNo` (0) | same wording for SIGABRT / SIGILL / SIGTRAP |
| `use_sigaltstack` | true | "If set, uses alternate stack for signal handling." |
| `log_path` | stderr | "Write logs to \"log_path.pid\". The special values are \"stdout\" and \"stderr\"." |
| `detect_leaks` | `!SANITIZER_APPLE` | "Enable memory leak detection." |
| `leak_check_at_exit` | true | "Invoke leak checking in an atexit handler. Has no effect if detect_leaks=false, or if __lsan_do_leak_check() is called before the handler has a chance to run." |
| `disable_coredump` | true on 64-bit | "Disable core dumping. By default, disable_coredump=1 on 64-bit to avoid dumping a 16T+ core file." |
| `print_summary` | true | "If false, disable printing error summaries in addition to error reports." |

([sanitizer_flags.inc](https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/sanitizer_common/sanitizer_flags.inc);
the wiki table agrees and adds that each tool "parses the common options from the
corresponding environment variable (ASAN_OPTIONS, TSAN_OPTIONS, MSAN_OPTIONS,
LSAN_OPTIONS)"
[SanitizerCommonFlags](https://github.com/google/sanitizers/wiki/SanitizerCommonFlags)).

Consequences for classification:

* **Linux, ASan report:** process exits `_exit(1)` (exit code 1 is the *only* signal;
  ASan sets `cf.exitcode = 1` explicitly,
  [asan_flags.cpp](https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/asan/asan_flags.cpp)).
  A user property that `exit(1)`s looks identical, so the supervisor needs the death
  callback or the report text (§6.3).
* **macOS, ASan report:** `abort_on_error` is true, so the worker dies with
  **SIGABRT** even when the underlying bug was a SEGV. **Observed:** Apple clang 21 ASan,
  null-page read: raw exit 134 (SIGABRT); `ASAN_OPTIONS=abort_on_error=0` gives exit 1;
  `ASAN_OPTIONS=handle_segv=0` gives 139 (SIGSEGV, the raw signal).
* **A real SIGSEGV under ASan** is caught by ASan's own SEGV handler (`handle_segv=1`),
  reported as "SEGV on unknown address", and then goes down the same `Die()` path — so
  a segfault in a sanitized worker never reaches the supervisor as `WTERMSIG == SIGSEGV`
  unless `handle_segv=0`. With `handle_segv=1` ("allow user to set own") the worker may
  install its own `SA_SIGINFO` handler *after* the runtime initialises and take over.
* **LSan standalone (`-fsanitize=leak`):** `cf.exitcode = 23` and `cf.detect_leaks = true`
  ([lsan.cpp](https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/lsan/lsan.cpp)),
  so a leaky process exits 23. Under ASan the leak check inherits ASan's `exitcode = 1`.
  **Observed** (Homebrew LLVM 23 on macOS): standalone LSan exit 23; ASan with
  `detect_leaks=1` exit 1.
* **UBSan:** its own flags are `halt_on_error` (default **false**: "Crash the program
  after printing the first error report"), `print_stacktrace` (false), `suppressions`,
  `report_error_type` (false), `silence_unsigned_overflow` (false)
  ([ubsan_flags.inc](https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/ubsan/ubsan_flags.inc)).
  By default the instrumented program "prints a verbose error report and continues
  execution upon a failed check"; `-fno-sanitize-recover=...` makes checks fatal
  ("print a verbose error report and exit the program"), `-fsanitize-trap=...` executes a
  trap instruction instead (SIGILL/SIGTRAP or `STATUS_ILLEGAL_INSTRUCTION` — no report)
  ([Clang UBSan](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)). A fatal
  UBSan check goes through `Die()` (exit 1, or SIGABRT on macOS). Options come from
  `UBSAN_OPTIONS` and `__ubsan_default_options()`
  ([ubsan_flags.cpp](https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/ubsan/ubsan_flags.cpp)).
  For qc, a *recovered* UBSan report is invisible to the supervisor unless it reads the
  report output or the worker uses `-fno-sanitize-recover` (GCC: "error recovery is
  turned on by default" for `-fsanitize=undefined`,
  [GCC Instrumentation Options](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html)).
* **ASan `halt_on_error`** is an ASan-specific flag, default true: "Crash the program
  after printing the first error report (WARNING: USE AT YOUR OWN RISK!)"
  ([asan_flags.inc](https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/asan/asan_flags.inc)).
  Setting it false requires `-fsanitize-recover=address` at compile time and is not
  useful for qc (the report, not continuation, is what matters).
* **GCC** uses the same libsanitizer runtime and env vars: "ASAN_OPTIONS ... help=1"
  lists options; `-fsanitize=leak` "only matters for linking of executables"
  ([GCC Instrumentation Options](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html)).

### 6.2 Windows (MSVC `/fsanitize=address` and clang-cl)

MSVC's runtime is "a regularly synced fork of the Clang AddressSanitizer runtime", so it
"implicitly inherits many of Clang's ASan runtime options", configured through
`ASAN_OPTIONS` or `__asan_default_options` (env var wins). Windows-specific additions:
`windows_fast_fail_on_error` ("set to true to enable the process to terminate with a
__fastfail(71) after printing the error report"), and the note "When abort_on_error value
is set to true, on Windows the program terminates with an exit(3)". `halt_on_error`
"doesn't function the way you might expect ... many error types are considered
non-continuable"; MSVC adds `continue_on_error` instead. `detect_container_overflow` and
`unmap_shadow_on_exit` are unsupported
([MSVC AddressSanitizer runtime](https://learn.microsoft.com/en-us/cpp/sanitizers/asan-runtime)).
So on Windows an ASan report normally ends in `_exit(1)` (Clang-inherited default
`exitcode=1`), or exit 3 with `abort_on_error=1`.

**ASan and SEH/VEH on Windows.** compiler-rt installs its exception machinery via
`AddVectoredExceptionHandler` (Win64: "On Win64, we map memory on demand with access
violation handler. Install our exception handler.") and by *intercepting*
`SetUnhandledExceptionFilter`; its `SEHHandler` reports deadly exceptions
(`IsHandledDeadlyException`) through `ReportDeadlySignal`, then chains to the user's
filter, and a comment notes "FIXME: Handle EXCEPTION_STACK_OVERFLOW here"
([asan_win.cpp](https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/asan/asan_win.cpp)).
The common `handle_segv` flags are "Ignored on Windows"
([SanitizerCommonFlags](https://github.com/google/sanitizers/wiki/SanitizerCommonFlags)).
A qc VEH registered with `First=1` runs before ASan's and can still capture the fault
address; returning `EXCEPTION_CONTINUE_SEARCH` lets ASan produce its report, after which
the process exits via `Die()` (exit 1) rather than with the NTSTATUS code. The runtime
DLL `clang_rt.asan_dynamic-{arch}.dll` must be present at run time even for `/MT` builds
([MSVC AddressSanitizer runtime](https://learn.microsoft.com/en-us/cpp/sanitizers/asan-runtime)).
MSVC ASan is "limited to x86 and x64 on Windows 10 and later" and incompatible with
`/RTC`, incremental linking, and Edit-and-Continue
([MSVC AddressSanitizer](https://learn.microsoft.com/en-us/cpp/sanitizers/asan),
[known issues](https://learn.microsoft.com/en-us/cpp/sanitizers/asan-known-issues)).

### 6.3 Hooks for the worker: death callback and report path

`sanitizer/common_interface_defs.h` declares:

```c
/// Sets the callback to be called immediately before death on error.
/// Passing 0 will unset the callback.
void SANITIZER_CDECL __sanitizer_set_death_callback(void (*callback)(void));
// Tell the tools to write their reports to "path.<pid>" instead of stderr.
void SANITIZER_CDECL __sanitizer_set_report_path(const char *path);
// Tell the tools to write their reports to the provided file descriptor
// (casted to void *).
void SANITIZER_CDECL __sanitizer_set_report_fd(void *fd);
// This function is called by the tool when it has just finished reporting
// an error. 'error_summary' is a one-line string that summarizes
// the error message. This function can be overridden by the client.
void SANITIZER_CDECL __sanitizer_report_error_summary(const char *error_summary);
```

([common_interface_defs.h](https://github.com/llvm/llvm-project/blob/main/compiler-rt/include/sanitizer/common_interface_defs.h)).
`Die()` calls the death callback first, before `abort()`/`_exit`
([sanitizer_termination.cpp](https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/sanitizer_common/sanitizer_termination.cpp)).
Design use: the worker registers a death callback that writes a "sanitizer" record to the
pipe, and overrides `__sanitizer_report_error_summary` to capture the one-line summary
(e.g. "AddressSanitizer: heap-buffer-overflow ...") into a static buffer that the death
callback then sends. `__sanitizer_set_report_fd` can point the full report at the pipe
(or a per-worker file via `__sanitizer_set_report_path`, note the `.<pid>` suffix). These
symbols exist only when a sanitizer runtime is linked, so declare them `weak` (GCC/Clang)
or resolve at run time on MSVC; the runtime also honours the exported functions on
Windows per Microsoft's docs (`__asan_default_options`, `ASAN_OPTIONS`)
([MSVC AddressSanitizer runtime](https://learn.microsoft.com/en-us/cpp/sanitizers/asan-runtime)).

### 6.4 LeakSanitizer availability and interface

**Upstream support.** LSan "can be combined with AddressSanitizer to get both memory error
and leak detection, or used in a stand-alone mode"; enable with `-fsanitize=address` plus
`ASAN_OPTIONS=detect_leaks=1` or `-fsanitize=leak`; supported platforms: Android, Fuchsia,
Linux, macOS, NetBSD — **Windows is not listed**
([Clang LeakSanitizer](https://clang.llvm.org/docs/LeakSanitizer.html)). The runtime
default `detect_leaks = !SANITIZER_APPLE` means leak checking is on by default on Linux
and off by default on macOS
([sanitizer_flags.inc](https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/sanitizer_common/sanitizer_flags.inc)).
If the platform cannot sanitize leaks, ASan refuses to start: "detect_leaks is not
supported on this platform." followed by `Die()`
([asan_flags.cpp](https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/asan/asan_flags.cpp)).

**Per compiler:**

* Linux GCC and Clang: yes (`-fsanitize=leak` / `detect_leaks`, GCC docs above).
* macOS, upstream LLVM clang (e.g. Homebrew): yes. **Observed:** LLVM 23 on arm64 macOS
  runs both standalone LSan (exit 23) and ASan+`detect_leaks=1` (exit 1).
* macOS, **Apple clang**: no. **Observed:** Apple clang 21.0.0 (Xcode 26.6): ASan with
  `ASAN_OPTIONS=detect_leaks=1` prints "AddressSanitizer: detect_leaks is not supported
  on this platform." and dies; `-fsanitize=leak` fails with "unsupported option
  '-fsanitize=leak' for target 'arm64-apple-darwin25.6.0'". Apple's Xcode documentation
  lists Address/Thread/Undefined Behavior sanitizers and does not offer a leak sanitizer
  (Apple's page could not be fetched for quotation; the observation above is the
  evidence).
* Windows MSVC: no LSan. Microsoft solicits feedback on "other sanitizers for the future,
  such as /fsanitize=thread, /fsanitize=leak, /fsanitize=memory, /fsanitize=undefined"
  ([MSVC AddressSanitizer](https://learn.microsoft.com/en-us/cpp/sanitizers/asan)).
* Windows clang-cl: ASan yes (Clang lists "Windows 8.1+"), UBSan yes (Clang lists
  Windows), LSan no (not in the LSan platform list)
  ([Clang ASan](https://clang.llvm.org/docs/AddressSanitizer.html),
  [Clang UBSan](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html),
  [Clang LSan](https://clang.llvm.org/docs/LeakSanitizer.html)).

**The `lsan_interface.h` API** (verbatim doc comments):

```c
// Check for leaks now. This function behaves identically to the default
// end-of-process leak check. In particular, it will terminate the process if
// leaks are found and the exitcode runtime flag is non-zero.
// Subsequent calls to this function will have no effect and end-of-process
// leak check will not run. Effectively, end-of-process leak check is moved to
// the time of first invocation of this function.
void SANITIZER_CDECL __lsan_do_leak_check(void);

// Check for leaks now. Returns zero if no leaks have been found or if leak
// detection is disabled, non-zero otherwise.
// This function may be called repeatedly, e.g. to periodically check a
// long-running process. It prints a leak report if appropriate, but does not
// terminate the process. It does not affect the behavior of
// __lsan_do_leak_check() or the end-of-process leak check, and is not
// affected by them.
int SANITIZER_CDECL __lsan_do_recoverable_leak_check(void);

// Allocations made between calls to __lsan_disable() and __lsan_enable() will
// be treated as non-leaks. Disable/enable pairs may be nested.
void SANITIZER_CDECL __lsan_disable(void);
void SANITIZER_CDECL __lsan_enable(void);
// The heap object into which p points will be treated as a non-leak.
void SANITIZER_CDECL __lsan_ignore_object(const void *p);
// The user may optionally provide this function to disallow leak checking
// for the program it is linked into (if the return value is non-zero).
int SANITIZER_CDECL __lsan_is_turned_off(void);
```

([lsan_interface.h](https://github.com/llvm/llvm-project/blob/main/compiler-rt/include/sanitizer/lsan_interface.h)).
Both entry points are no-ops (returning 0) unless `CAN_SANITIZE_LEAKS` and
`common_flags()->detect_leaks`
([lsan_common.cpp](https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/lsan/lsan_common.cpp)).
For a long-lived worker that runs many test cases, `__lsan_do_recoverable_leak_check()`
after each case is the right primitive: it reports leaks *for that case* without killing
the worker, and its non-zero return lets the worker send a "leak" record over the pipe so
the supervisor can treat the case as a failure and keep shrinking. The end-of-process check
then only reports leaks that the per-case checks already saw plus anything left at exit.
Wrap the worker's own bookkeeping allocations in `__lsan_disable()/__lsan_enable()` or
`__lsan_ignore_object` so they never appear as leaks of the property.

### 6.5 Feature-detection macros

| compiler | ASan | UBSan | LSan | source |
|---|---|---|---|---|
| Clang | `__has_feature(address_sanitizer)` = `hasOneOf(Address \| KernelAddress)`; also `__SANITIZE_ADDRESS__` in recent clang | `__has_feature(undefined_behavior_sanitizer)` = `hasOneOf(Undefined & ~UBSanFeatureIgnoredSanitize)`; per-check names such as `null_sanitizer`, `signed_integer_overflow_sanitizer` | `__has_feature(leak_sanitizer)` = `has(Leak)` — **true only for `-fsanitize=leak`, not for ASan-with-leak-checking** | [Features.def](https://github.com/llvm/llvm-project/blob/main/clang/include/clang/Basic/Features.def); `__SANITIZE_ADDRESS__`, `__SANITIZE_HWADDRESS__`, `__SANITIZE_THREAD__` defined in [InitPreprocessor.cpp](https://github.com/llvm/llvm-project/blob/main/clang/lib/Frontend/InitPreprocessor.cpp); "Note, __has_feature test for sanitizers is deprecated, and Clang will support __SANITIZE_<sanitizer>__ similar to GCC" ([Clang ASan docs](https://clang.llvm.org/docs/AddressSanitizer.html)) |
| GCC | `__SANITIZE_ADDRESS__` "defined, with value 1, when -fsanitize=address or -fsanitize=kernel-address are in use" | none documented | none documented | [GCC Common Predefined Macros](https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html) (also `__SANITIZE_THREAD__`) |
| MSVC | `__SANITIZE_ADDRESS__` "Available beginning with Visual Studio 2019 version 16.9. Defined as 1 when the /fsanitize=address compiler option is set" | n/a (unsupported) | n/a (unsupported) | [MSVC Predefined macros](https://learn.microsoft.com/en-us/cpp/preprocessor/predefined-macros), [asan-building](https://learn.microsoft.com/en-us/cpp/sanitizers/asan-building) |

**Observed** (macOS): Homebrew LLVM 23 with `-fsanitize=address,undefined` gives
`__has_feature(address_sanitizer)=1`, `undefined_behavior_sanitizer=1`,
`leak_sanitizer=0`, and defines `__SANITIZE_ADDRESS__`; Apple clang 21 gives the same
`__has_feature` results but does **not** define `__SANITIZE_ADDRESS__`. Portable detection
for qc's headers is therefore:

```c
#if defined(__SANITIZE_ADDRESS__)
#  define QC_ASAN 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define QC_ASAN 1
#  endif
#endif
#if defined(__has_feature)
#  if __has_feature(undefined_behavior_sanitizer)
#    define QC_UBSAN 1
#  endif
#endif
/* GCC has no UBSan macro: detect UBSan at run time (weak __ubsan_* symbol) or via a build define. */
```

Note that `__has_feature` must be tested with `defined(__has_feature)` first because MSVC
and GCC do not provide it; the Clang docs' own example uses that shape
([Clang ASan docs](https://clang.llvm.org/docs/AddressSanitizer.html)).

---

## 7. Summary of what this means for qc

1. **Spawn:** `posix_spawn` on Linux and macOS (never `fork` without `exec` on macOS;
   Apple's own guidance), re-executing `/proc/self/exe` on Linux and the
   `_NSGetExecutablePath` result on macOS; `CreateProcessW` with an explicit
   `lpApplicationName` from `GetModuleFileNameW(NULL)` on Windows. Treat exit 127 as
   "exec failed" on Unix.
2. **Pipes:** `pipe`+`FD_CLOEXEC` (or `pipe2(O_CLOEXEC)`) with `posix_spawn_file_actions_adddup2`;
   `CreatePipe` with inheritable child ends, `SetHandleInformation` to un-inherit the
   parent ends, `STARTF_USESTDHANDLES` or `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`, and close
   the child's ends in the parent.
3. **The parent never learns the fault address.** `waitpid` yields only `WTERMSIG`;
   `GetExitCodeProcess` yields only the NTSTATUS. The worker must report it itself: a
   `SA_SIGINFO` handler on SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGTRAP on an alternate stack that
   does only `write` + `raise` (async-signal-safe), and an `AddVectoredExceptionHandler(1, ...)`
   that reads `EXCEPTION_RECORD.ExceptionInformation[1]` and returns
   `EXCEPTION_CONTINUE_SEARCH`.
4. **Classification order:** (a) a structured record on the pipe (fault, abort, leak,
   sanitizer, or clean case result) is authoritative; (b) otherwise on Unix
   `WIFSIGNALED` → signal death (SIGKILL sent by us = timeout, SIGABRT = abort/assert or
   macOS sanitizer death), `WIFEXITED` → exit code (1 may be a sanitizer, 23 an LSan leak,
   127 exec failure); on Windows `0xC…` NTSTATUS → fault, 3 → abort/assert, our
   `TerminateProcess` code → timeout, else plain exit.
5. **Windows dialogs:** worker calls `SetErrorMode(SEM_FAILCRITICALERRORS |
   SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX)`, `_set_abort_behavior(0,
   _WRITE_ABORT_MSG | _CALL_REPORTFAULT)`, `_set_error_mode(_OUT_TO_STDERR)`, and under
   `_DEBUG` `_CrtSetReportMode(_CRT_ASSERT|_CRT_ERROR, _CRTDBG_MODE_FILE)` to stderr;
   the supervisor can additionally use a job object with `KILL_ON_JOB_CLOSE |
   DIE_ON_UNHANDLED_EXCEPTION`.
6. **Timeouts:** `poll` on the pipe (POLLHUP = worker died) with a millisecond deadline,
   then `kill(SIGKILL)` + `waitpid`; optionally `pidfd_open` (Linux ≥ 5.3) or kqueue
   `EVFILT_PROC` (macOS) in the same wait set. Windows: `WaitForSingleObject(hProcess,
   slice)` interleaved with `PeekNamedPipe`, or an overlapped named pipe with
   `WaitForMultipleObjects`; `TerminateProcess` then wait again.
7. **Sanitizers:** register `__sanitizer_set_death_callback` (weak) to emit a
   "sanitizer" record, capture the summary via `__sanitizer_report_error_summary`, and
   run `__lsan_do_recoverable_leak_check()` after each test case. Remember
   `abort_on_error` is true on macOS (SIGABRT, not SIGSEGV, reaches the supervisor),
   ASan's exit code is 1 everywhere, LSan standalone is 23, UBSan recovers by default
   (use `-fno-sanitize-recover` or read the report), Apple clang and MSVC have no LSan,
   and clang-cl has ASan/UBSan but no LSan.
