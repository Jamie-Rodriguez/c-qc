# qc design map

Label: wayfinder:map
Tracker: local markdown (see docs/agents/issue-tracker.md)

## Destination

A design spec plus ADRs for `qc`, a property-based testing library for C, covering its public API, supervisor-and-worker isolation model, Hypothesis-style typed-choice generation and shrinking, and a Linux/macOS/Windows portability strategy, sharp enough to begin test-driven implementation. Implementation is a separate effort.

## Notes

**Domain.** Property-based testing in C. Glossary in `CONTEXT.md` at the repo root; decisions in `docs/adr/`. Research findings land in `docs/research/<name>.md`.

**Skills to consult.** `/grilling` and `/domain-modeling` for every decision ticket. `/prototype` for the header prototype. `/research` for research tickets. `/codebase-design` when a ticket draws a module seam. The repo's `coding-standards` skill governs any code written, including prototypes and the CI bootstrap.

**Standing constraints fixed while charting** (revisit only by reopening them explicitly):

- Audience: publishable open source, designed for the author's own use first. Ergonomics the author wants are not vetoed by hypothetical users.
- Name and prefix: `qc`, `qc_` for functions and types, `QC_` for macros. Licence MIT.
- Language: strict ISO C99 in the public header. No `_Generic`, no variable-length arrays, no compiler extensions in the header. Extensions allowed internally only behind feature checks.
- Toolchains: supported are GCC and Clang on Linux, Clang on macOS, MSVC and clang-cl on Windows. MSVC floor is Visual Studio 2015. MinGW-w64 is best-effort.
- Build: GNU Make for Unix plus a Windows-only CMake file. Source tree is multi-file; releases amalgamate to one header plus one `.c`.
- API shape: draw-based. A property takes a `qc_t *` and pulls values inside its body via explicitly typed draw functions. Failure and skip via `QC_ASSERT` and `QC_ASSUME` macros; property bodies are `void` (the `bool` return form was dropped by the header prototype ticket). Collections use a `qc_more` loop, no callbacks. Draws are unnamed by default; an optional `qc_label` call names the next draw for reports.
- Runner: `qc_check` works from any `main` provided `argc`/`argv` are forwarded to `qc_init`; `QC_MAIN` is the zero-boilerplate path. Single-threaded, one worker.
- Isolation: supervisor and worker processes (ADR-0001). The worker is the same binary re-executed in worker mode. Choices stream to the supervisor as they are drawn (ADR-0002).
- Failure classes in scope: fatal signals and Windows structured exceptions, `abort` and failed `assert`, hangs via a per-case deadline (default 1 s, configurable, shrunk like any failure), `exit` from inside a property, and sanitizer reports. Sanitizer integration means captured stderr plus per-case leak attribution via LeakSanitizer's recoverable check where the compiler supports it.
- Crash report contents: signal or exception name, fault address where the platform gives it, captured stderr.
- Generation and shrinking: typed choices, following Hypothesis's current design rather than its older byte-stream model. Strings come in three generators: raw bytes, ASCII, and UTF-8 drawn as code points.
- Run defaults: 100 test cases per property, overridable per property and by environment variable. Failure database in `.qc/` in the working directory, one file per property keyed by name, replayed first by default, disabled by environment variable.
- Testing the library: a minimal in-repo assertion harness for the engine and worker protocol, then self-hosted `qc` properties for generators and shrinking once the core runs.

## Decisions so far

<!-- one line per closed ticket: gist plus link to the ticket that holds the detail -->
- [Bootstrap the repo and three-OS CI](issues/01-bootstrap-repo-and-ci.md) — repo is https://github.com/Jamie-Rodriguez/c-qc; `.github/workflows/ci.yml` runs five jobs (Linux GCC and Clang, macOS Clang via `make check`; Windows MSVC and clang-cl via CMake) and the first run was green; Windows is CMake-only so nothing was reconciled with GNU Make there; MSVC ignores `CMAKE_C_STANDARD 99` so C99 strictness is enforced by the GCC/Clang jobs; the VS2015 floor is not exercised by CI.
- [Survey prior art in C property-based testing](issues/02-research-prior-art-c-pbt.md) — no existing C library combines shrinking with portable crash tolerance; theft has both but depends on `fork` and fails under MSVC; draw-in-the-body APIs over a recorded stream already exist in C (qcc, tapc, Hegel), validating the choice-sequence plan; theft's untyped stream shows why typed choices matter; re-spawning the executable and reading a verdict back is the portable protocol shape; macOS CrashReporter slows crash shrinking badly; a printable replay blob is expected by users of RapidCheck, Proptest, and Hegel. Findings in `docs/research/prior-art-c-pbt.md`.
- [Document process spawning, crash detection, timeouts, and sanitizer exit behaviour per OS](issues/05-research-process-and-crash-mechanics.md) — the parent never learns a fault address, so the worker must report it from an async-signal-safe `SA_SIGINFO` handler or a vectored exception handler; spawn with `posix_spawn` and `CreateProcessW`; suppress Windows crash and assert dialogs; timeouts via `poll` plus `SIGKILL` or `WaitForSingleObject` plus `TerminateProcess`; sanitizers exit 1 (SIGABRT on macOS by default); LeakSanitizer exists on Linux only. Findings in `docs/research/process-and-crash-mechanics.md`.
- [Document Hypothesis's typed choice sequence](issues/03-research-hypothesis-typed-choices.md) — a test case is a flat list of choice nodes over five types (integer, float, boolean, string, bytes), each carrying its constraints; one index ordering per type defines "simpler"; collections record a boolean "more" choice before each element; replay takes a value prefix and treats a constraint violation as a misalignment; the database stores values only and re-derives constraints by replay. Findings in `docs/research/hypothesis-typed-choices.md`.
- [Document Hypothesis's shrinker](issues/04-research-hypothesis-shrinker.md) — shortlex-minimal choice sequence; a candidate is accepted only if it fails with the same interesting origin and is strictly smaller; greedy fixed-point loop over ~11 passes with a stall budget; minimal port is the ordering, acceptance rule, fixed-point loop, `find_integer`, contiguous-deletion passes, per-choice minimisation, trivial spans, and duplicate minimisation. Findings in `docs/research/hypothesis-shrinker.md`.
- [Prototype the public header](issues/06-prototype-public-header.md) — the draw-based API reads well; accepted `QC_PROPERTY(id)` with the handle fixed to `qc`, `void` bodies with `QC_ASSERT`/`QC_FAIL`/`QC_ASSUME` (the `bool` return form is dropped), value-returning draws, `qc_span_t` for bytes owned by the test case, explicit `qc_list_t` handle with `qc_more(&list)`, `qc_label` naming the next draw, and `QC_MAIN(a, b, c)` listing names via a one-element-array trick. Prototype on branch `prototype/header`.

## Not yet specified

- Failure database file format, and the encoding of a choice sequence as a portable blob.
- The environment-variable and configuration surface: names, precedence between per-property settings and global overrides.
- PRNG choice and seeding.
- Per-OS timeout implementation: how the supervisor kills a hung worker on each platform.
- Float generation and shrinking details: NaN and infinity policy, subnormals, the shrink order for floats.
- Which further decisions warrant ADRs beyond 0001 and 0002.
- Whether and how to check the MSVC 2015 floor, since CI only has Visual Studio 2022.

## Out of scope

- Signature-style property macros (RapidCheck-style parameter lists). Draw-based core only in this effort.
- Stateful and model-based testing.
- Integration with other C test frameworks (Unity, cmocka, Criterion, greatest).
- Coverage-guided and targeted generation.
- Stack overflow detection and reporting.
- In-process replay of a counterexample for debuggers. Noted as future work.
- Backtraces in crash reports.
- Parallel workers and thread safety.
- MinGW-w64 beyond best-effort.
