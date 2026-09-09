# Survey prior art in C property-based testing

Type: research
Status: resolved
Blocked by: none

## Question

What do existing C property-based testing libraries do, and what should `qc` learn or avoid? Cover at least theft (Scott Vokes) and any other maintained C libraries found. For each: how properties are declared, how values are drawn or generated, how shrinking works, whether crashing test cases are tolerated and how (fork, signals, nothing), Windows support, licence, and maintenance status.

Also cover the ergonomics of RapidCheck (C++) and Proptest (Rust) only as far as they suggest API shapes that can survive translation to C99.

Findings go in `docs/research/prior-art-c-pbt.md` with a source for every claim.

## Answer

No existing C library combines shrinking with crash tolerance portably: theft (ISC, last tag 2019, last push 2020) has both but relies on `fork`/`pipe`/`poll` and fails to build under MSVC (open issue #62); qcc, tapc, quickcheck4c, and mcandre/qc have no process isolation, and foobarbaz catches `SIGSEGV` in-process with `siglongjmp`.
Draw-in-the-body APIs over a recorded stream already exist in C (qcc's `GIVEN_UINT` with intervals, tapc's `tap_read_*` with a chunk table, Hegel's `hegel_generate_*` C ABI) and all shrink by editing the stream and re-running the generator, validating qc's choice-sequence plan; theft's autoshrink shows the cost of an untyped stream (blind bit mutations, "all-zero bits must be minimal" rule).
For the worker protocol, theft's one-byte verdict over a pipe conflates crash with failure; Proptest (rusty-fork) and DeepState's Win32 path instead re-spawn the executable and read a verdict or replay log back, which is the portable shape for Linux/macOS/Windows.
Timeouts need a two-stage signal-then-`SIGKILL` (theft) and a decision on late passes; macOS CrashReporter makes crash-shrinking orders of magnitude slower (theft docs).
RapidCheck's `reproduce=` string, Proptest's `proptest-regressions` files, and Hegel's "reproduce blob" all say the printable, replayable choice sequence is a first-class deliverable.

Full findings: [docs/research/prior-art-c-pbt.md](../../../docs/research/prior-art-c-pbt.md)
