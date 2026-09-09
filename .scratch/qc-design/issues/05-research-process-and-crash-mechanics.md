# Document process spawning, crash detection, timeouts, and sanitizer exit behaviour per OS

Type: research
Status: resolved
Blocked by: none

## Question

For Linux, macOS, and Windows, what are the primary-source facts a supervisor process needs to spawn a worker from the same executable, talk to it over a pipe, detect how it died, and kill it on a deadline?

Cover: spawning (`posix_spawn` or `fork`/`exec` versus `CreateProcess`), pipe creation and inheritance, how a fatal signal is reported to the parent (`waitpid` status) versus how a Windows structured exception is reported (exit code as NTSTATUS, and what a vectored exception handler in the worker can add such as the fault address), how `abort` and a failed `assert` exit on each platform including the MSVC CRT's assert dialog and how to suppress it, timeouts (`kill` versus `TerminateProcess`, and waiting with a deadline), and the exit behaviour of AddressSanitizer and UndefinedBehaviorSanitizer on each platform including which compilers support LeakSanitizer and the `__lsan_do_recoverable_leak_check` interface, and the feature macros that detect a sanitized build.

Findings go in `docs/research/process-and-crash-mechanics.md` with a source for every claim.

## Answer

The parent never learns a fault address: `waitpid` yields only `WTERMSIG` and `GetExitCodeProcess` only the NTSTATUS, so the worker must report it itself via an `SA_SIGINFO` handler (alternate stack, `write` + `raise` only) on Unix and a first-position vectored exception handler reading `ExceptionInformation[1]` on Windows. Spawn with `posix_spawn` (Apple says never fork-without-exec) re-executing `/proc/self/exe` or `_NSGetExecutablePath`, and `CreateProcessW` with `GetModuleFileNameW(NULL)`; `FD_CLOEXEC` + `adddup2` and `SetHandleInformation` keep pipe ends from leaking. `abort` is `WTERMSIG == SIGABRT` on Unix and exit code 3 on Windows once `_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT)`, `SetErrorMode(SEM_NOGPFAULTERRORBOX | ...)` and `_set_error_mode(_OUT_TO_STDERR)` suppress the dialogs. Timeouts: `poll` on the pipe (POLLHUP = worker died) then `kill(SIGKILL)`; `pidfd_open` / kqueue `EVFILT_PROC` are optional; on Windows `WaitForSingleObject` + `PeekNamedPipe` (anonymous pipes cannot be overlapped) then `TerminateProcess`. Sanitizers exit 1 via `Die()` (LSan standalone 23), but `abort_on_error` defaults to true on macOS so a sanitized SEGV arrives as SIGABRT; hook `__sanitizer_set_death_callback` and run `__lsan_do_recoverable_leak_check()` per test case. LSan exists on Linux GCC/Clang and upstream LLVM on macOS but not Apple clang, MSVC, or clang-cl; detect ASan with `__SANITIZE_ADDRESS__` (GCC, MSVC, new Clang) falling back to `__has_feature(address_sanitizer)`, and UBSan with `__has_feature(undefined_behavior_sanitizer)` (GCC has no macro).

Full findings: [docs/research/process-and-crash-mechanics.md](../../../docs/research/process-and-crash-mechanics.md)
