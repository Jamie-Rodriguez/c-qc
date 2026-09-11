# Header prototype: the shape chosen and the shapes not chosen

PROTOTYPE. Throwaway. Each section shows the same property in the shape
`qc.h` uses and in the alternatives considered. React to these; the ticket's
answer records which survive.

Run it: `make -C prototype/header run` from the repo root.

## 1. Property definition and the handle's name

**Chosen: `QC_PROPERTY(id)` fixes the handle to `qc`; macros use it implicitly.**

```c
QC_PROPERTY(addition_commutes)
{
    int32_t a = qc_int32(qc, INT32_MIN, INT32_MAX);
    int32_t b = qc_int32(qc, INT32_MIN, INT32_MAX);
    QC_ASSERT(a + b == b + a);
}
```

**Alternative A: plain function, explicit handle in every macro.**

```c
static void addition_commutes(qc_t *t)
{
    int32_t a = qc_int32(t, INT32_MIN, INT32_MAX);
    int32_t b = qc_int32(t, INT32_MIN, INT32_MAX);
    QC_ASSERT(t, a + b == b + a);
}
/* and later */
qc_check("addition_commutes", addition_commutes);
```

No macro magic and the user picks the handle's name, at the cost of `t,` in
every assertion and a name string that can drift from the function name. The
name string keys the failure database, so drift silently orphans saved
counterexamples.

**Alternative B: `QC_PROPERTY(id)` but macros still take the handle.**

Combines the costs of both. Not recommended.

## 2. How a property reports failure

**Chosen: `void` body; `QC_ASSERT` / `QC_FAIL` record and `return`.**

C99 forbids `return expr;` in a `void` function and requires it elsewhere, so
one macro cannot serve both a `void` and a `bool` property. The macros commit
to `void`.

**Alternative: `bool` body, failure is `return false`.**

```c
static bool addition_commutes(qc_t *t)
{
    int32_t a = qc_int32(t, INT32_MIN, INT32_MAX);
    int32_t b = qc_int32(t, INT32_MIN, INT32_MAX);
    return a + b == b + a;
}
```

Reads beautifully for one-line properties and loses the file:line and the
asserted expression in the report. It could exist alongside the `void` shape
as `QC_PROPERTY_BOOL(id)` with a matching `QC_CHECK_BOOL`, but then there are
two property types, two `QC_MAIN` entry shapes, and macros that only work in
one of them. The charting note said "`QC_ASSERT` and `QC_ASSUME` macros, or a
`bool` return"; this prototype proposes dropping the `bool` return.

**Helper functions.** A helper without `qc` in scope calls `qc_fail(qc, ...)`
directly and returns on its own; the caller must then also return. A second
macro, `QC_ASSERT_OK(helper(qc, x))`, could make that idiom one line.

## 3. Draw functions: value or status?

**Chosen: draws return the value.**

```c
int32_t n = qc_int32(qc, 0, 100);
```

Possible only because the worker is a separate process (ADR-0001): when the
engine wants to abandon a test case mid-body (choice budget exceeded, replay
misaligned), the worker can leave the case out of band and the property never
sees a status. Hegel's `hegel_generate_integer(ctx, tc, min, max, &out)` and
its `HEGEL_E_STOP_TEST` return exist because Hegel is in-process.

**Alternative: out-parameter and a status.**

```c
int32_t n;
if (!qc_draw_int32(qc, 0, 100, &n)) return;
```

Rejected: every draw becomes two lines plus a branch, for a status the user
can do nothing with.

## 4. Integer ranges

**Chosen: one function, always ranged, both bounds inclusive.**

```c
int32_t any = qc_int32(qc, INT32_MIN, INT32_MAX);
int32_t small = qc_int32(qc, 0, 100);
```

**Alternative: an unranged form too.**

```c
int32_t any = qc_int32_any(qc);
```

Saves typing `INT32_MIN, INT32_MAX` once per property at the cost of a second
name per integer type (`qc_int32_any`, `qc_uint32_any`, `qc_int64_any`, ...).
The v1 generator set ticket decides which integer types exist; this ticket
only decides the shape.

## 5. Byte strings

**Chosen: a small struct by value; the test case owns the buffer.**

```c
qc_span_t input = qc_bytes(qc, 0, 1024);
use(input.data, input.len);
```

No `free`. The buffer lives until the property returns. The property may write
into it, which matters for sort-in-place style code.

**Alternative: pointer return, length out-parameter.**

```c
size_t len;
uint8_t *input = qc_bytes(qc, 0, 1024, &len);
```

Idiomatic C, one more local per draw. `qc_span_t` would also be the natural
return type of the ASCII and UTF-8 string generators, so the struct earns its
name three times over.

## 6. Collections

**Chosen: an explicit loop handle.**

```c
int32_t xs[32];
qc_list_t list = qc_list(qc, 0, 32);
while (qc_more(&list)) {
    xs[list.len] = qc_int32(qc, -100, 100);
}
sort(xs, list.len);
```

`list.len` is the append index inside the loop and the count after it, the
same shape as `xs[len++]`.

**Alternative A: no handle, the engine keeps a loop stack.**

```c
size_t n = 0;
while (qc_more(qc, 0, 32)) {
    xs[n++] = qc_int32(qc, -100, 100);
}
```

Shortest. Broken by two sequential loops inside an outer loop: after the first
inner loop ends, the engine cannot tell whether the next `qc_more` continues
the outer loop or opens the second inner one.

**Alternative B: no handle, a macro keys the loop on `__FILE__`/`__LINE__`.**

```c
while (QC_MORE(0, 32)) { ... }
```

Fixes the sibling-loop case, breaks on recursion: a recursive generator (a
tree, a nested document) re-enters the same line and looks like a
continuation of itself.

**Alternative C: element callback.**

```c
qc_list_of(qc, 0, 32, draw_element, &ctx);
```

Ruled out while charting: callbacks are the thing the draw-based API exists to
avoid.

## 7. Labels

**Chosen: `qc_label` names the next draw; unnamed draws print as `#n`.**

```c
qc_label(qc, "index");
int32_t i = qc_int32(qc, 0, len - 1);
```

A label before a list names the list; elements print as `xs[0]`, `xs[1]`.

**Alternative A: a name parameter on every draw.**

```c
int32_t i = qc_int32(qc, "index", 0, len - 1);
```

Every draw pays for the label, including the majority nobody names. `NULL` for
unnamed reads as noise.

**Alternative B: a labelled variant of every draw function.**

```c
int32_t i = qc_int32_named(qc, "index", 0, len - 1);
```

Doubles the generator surface.

## 8. Registration for `QC_MAIN` without constructor attributes

**Chosen: `QC_MAIN` takes the list; `QC_PROPERTY` makes each name a one-element array.**

```c
QC_MAIN(sort_is_idempotent, binary_search_finds_present_element)
```

`QC_PROPERTY(id)` defines `static const qc_property_t id[1]`, so a bare `id`
decays to `const qc_property_t *` in both `qc_check(id)` and the `QC_MAIN`
initialiser. Pure C99: variadic macros are C99, and an array name is an
address constant. No constructors, no linker sections, no registry.

The cost is that a property is listed twice (defined, then named in
`QC_MAIN`), and a property defined but not listed is silently never run. The
compiler helps a little: an unlisted property is an unused `static` and warns
under `-Wunused`.

**Alternative A: `&` at every use.**

```c
QC_MAIN(&sort_is_idempotent, &binary_search_finds_present_element)
qc_check(&sort_is_idempotent);
```

Same mechanism without the array trick. Honest but noisy.

**Alternative B: constructor attributes where available, manual list elsewhere.**

Two registration paths, and the MSVC path is a `#pragma section` trick that
the C99-in-the-header rule forbids.

This choice would close the map's "how properties register for `QC_MAIN` in
C99" fog item if accepted.

## 9. Two things the prototype does not show

- **Per-property settings** (case count, deadline). A natural spot is a third
  field in `qc_property_t` set by a `QC_PROPERTY_WITH(id, .cases = 1000)`
  designated-initialiser macro. Deferred to the configuration-surface fog
  item.
- **Strings.** `qc_ascii` and `qc_utf8` would return `qc_span_t` too, with
  `data` NUL-terminated for convenience. Deferred to the v1 generator set
  ticket.
