# qc

A property-based testing library for C that survives crashing test cases and shrinks them to a minimal counterexample. Generation and shrinking follow Hypothesis.

## Language

**qc**:
The library's name and the prefix on every public symbol (`qc_` for functions and types, `QC_` for macros).
_Avoid_: qc-c, quickcheck

**Property**:
A statement about a program that should hold for every input drawn from its generators. The unit a user writes and the library checks.
_Avoid_: test, spec

**Counterexample**:
A concrete input for which a property fails. What the library reports after shrinking.
_Avoid_: failing input, repro

**Shrinking**:
The search for a smaller counterexample once a failure is found.
_Avoid_: minimisation, reduction

**Test case**:
One execution of a property against one drawn input.
_Avoid_: example, run

**Choice sequence**:
The recorded list of typed choices that fully determines a test case. Replaying it reproduces the case exactly.
_Avoid_: byte stream, seed data

**Generator**:
Something a property draws a value from.
_Avoid_: strategy, arbitrary

**Worker**:
The child process that executes test cases.
_Avoid_: child, subprocess

**Supervisor**:
The parent process that runs the engine: generation, shrinking, and reporting.
_Avoid_: parent, driver, host
