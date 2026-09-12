# Prototype the public header

Type: prototype
Status: resolved
Blocked by: none

## Question

Write a throwaway `qc.h` and two example properties to react to, so the ergonomics can be judged before the engine exists. Show: the `qc_t *` draw-based property signature, explicitly typed draw functions for at least `int32_t`, `bool`, and bytes, `QC_ASSERT` and `QC_ASSUME`, the `qc_more` collection loop, `qc_label`, `qc_check` from a user `main` with `qc_init(argc, argv)`, and `QC_MAIN`. Strict C99, no extensions.

The question this resolves: does the draw-based API read well in real C, and what changes before it becomes the spec? The answer records the accepted shape and the rejected variants.

## Comments

### 2026-09-11 — prototype built

Throwaway prototype on branch `prototype/header` (commit `0f754e2`), directory `prototype/header/`: `qc.h` (candidate header), `qc_stub.c` (in-process random stub engine, no shrinking), `example_main.c` (`QC_MAIN` path), `example_rle.c` (user `main` path), `VARIANTS.md` (chosen shape beside the alternatives). Run with `make -C prototype/header run`. Compiles clean under Apple clang and Homebrew clang with `-std=c99 -pedantic -Wall -Wextra -Werror`.

### 2026-09-12 — human reacted, all six shapes accepted

## Answer

**The draw-based API reads well in real C.** Two example files with seven properties, including a dependent draw (an index bounded by a length drawn earlier), needed no contortions. Accepted shape, all points confirmed by the author:

1. **Property definition.** `QC_PROPERTY(id) { ... }` defines `static void qc_body_id(qc_t *qc)` and fixes the handle's name to `qc`. The identifier is the property's name in reports and the failure database.
2. **Bodies are `void`.** Failure and skip go through `QC_ASSERT(cond)`, `QC_FAIL(message)`, and `QC_ASSUME(cond)`, which record the outcome and `return`. The `bool`-return property form from the charting constraints is **dropped**: a macro cannot know its enclosing function's return type, so one return type had to win, and `void` keeps file, line, and expression in the report. `qc_fail` and `qc_skip` are public for helpers that lack the `qc` name; the caller still returns on its own.
3. **Draws return the value.** `int32_t qc_int32(qc_t *, int32_t min, int32_t max)` with both bounds inclusive and no unranged twin; `bool qc_bool(qc_t *)`. No status channel: the worker process lets the engine abandon a case out of band (ADR-0001).
4. **Byte strings are a struct by value.** `qc_span_t { uint8_t *data; size_t len; } qc_bytes(qc_t *, size_t min_len, size_t max_len)`. The test case owns the buffer, frees it when the property returns, and the property may write into it. The ASCII and UTF-8 generators should return `qc_span_t` too.
5. **Collections use an explicit handle.** `qc_list_t list = qc_list(qc, min, max); while (qc_more(&list)) { xs[list.len] = ...; }`. `list.len` is the append index inside the loop and the count after it. One recorded "more?" boolean precedes each element, forced true below `min` and forced false at `max`. Lists nest.
6. **Labels are a separate call.** `qc_label(qc, "name")` names the next draw only; unnamed draws print as `#n`; a label before a list names the list and elements print as `name[i]`.
7. **Registration for `QC_MAIN` is by listing names.** `QC_MAIN(a, b, c)` expands to a `main` over a static pointer array. `QC_PROPERTY` defines each name as a one-element `const qc_property_t` array so the bare name decays to a pointer in both `QC_MAIN(...)` and `qc_check(name)`. Pure C99 (variadic macros plus address constants); no constructor attributes. An unlisted property is never run and shows up as an unused-static warning.
8. **Running.** `qc_init(argc, argv)`, then `bool qc_check(const qc_property_t *)` per property, then `int qc_finish(void)` for the summary and exit status. `QC_MAIN` is exactly that sequence.

**Rejected variants** (detail in `VARIANTS.md` on the branch): explicit-handle macros `QC_ASSERT(t, cond)` with `qc_check("name", fn)`; `bool` bodies; out-parameter draws with a status; `qc_int32_any`; pointer-plus-length-out bytes; handle-free `qc_more(qc, min, max)` (cannot separate sibling loops); `__LINE__`-keyed `QC_MORE` (breaks on recursion); element callbacks; a name parameter or a `_named` twin on every draw; `&` in `QC_MAIN`; constructor-attribute registration.

**Deferred, not decided here:** per-property settings (a candidate is a third `qc_property_t` field set by a designated-initialiser macro), which belongs to the configuration-surface fog item; the string generators, which belong to the v1 generator set ticket.

**ADR candidate:** the `void`-body decision and the reasons it beat `bool` are the kind of thing a reader will ask about; the spec-assembly ticket should consider an ADR for the property return shape.
