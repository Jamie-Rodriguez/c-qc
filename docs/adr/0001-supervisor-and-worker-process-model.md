---
status: accepted
---

# Supervisor and worker process model

`qc` must survive a test case that segfaults, aborts, hangs, or trips a sanitizer, on Linux, macOS, and Windows, and then keep shrinking. We run the engine (generation, shrinking, reporting) in a supervisor process and execute test cases in a long-lived worker process that is the same binary re-executed in a worker mode, respawned whenever it dies.

## Considered options

- **In-process signal handlers with `longjmp`.** Rejected: unavailable for Windows structured exceptions, undefined behaviour after a segfault, and incompatible with sanitizers.
- **Fork per test case on Unix, `CreateProcess` per case on Windows.** Rejected: an order of magnitude slower, and two divergent code paths. A Unix `fork` fast path may return later as an optimisation inside the supervisor model.

## Consequences

The test binary must recognise a worker-mode invocation, so `qc_init` needs `argc` and `argv`. Test cases cannot share in-process state with the runner, and a property's side effects happen in the worker.
