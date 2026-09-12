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

**Draw**:
One call inside a property that pulls a typed value from the test case: `qc_int32`, `qc_bool`, `qc_bytes`. Each draw records one or more choices.
_Avoid_: generate, sample

**Label**:
An optional name given to the next draw with `qc_label`, used only when reporting a counterexample. Unnamed draws report by position.
_Avoid_: tag, annotation

**List**:
A collection drawn element by element through a `qc_list_t` handle and a `qc_more` loop. Each element is preceded by a recorded "more?" choice.
_Avoid_: array generator, vector

**Span**:
A byte buffer with a length, `qc_span_t`, returned by the byte and string generators and owned by the test case.
_Avoid_: slice, buffer

**Worker**:
The child process that executes test cases.
_Avoid_: child, subprocess

**Supervisor**:
The parent process that runs the engine: generation, shrinking, and reporting.
_Avoid_: parent, driver, host
