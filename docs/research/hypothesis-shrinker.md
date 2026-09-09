# How Hypothesis shrinks a choice sequence

Research for qc's shrinking design. Every claim cites the Hypothesis source at
commit `cd434f23be1a3598085cf096e28e6738c63b29b3` (master, 2026-09-09). The
repository was restructured recently: the conjecture package now lives at
`hypothesis/src/hypothesis/internal/conjecture/` (the old
`hypothesis-python/src/...` path returns 404). All links below are permalinks
to that commit.

Base URL for links: `https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/` — abbreviated `CONJ/` in link text but written out in full in every link.

## Vocabulary

Hypothesis word → qc word (from `CONTEXT.md`):

| Hypothesis | qc |
|---|---|
| strategy | generator |
| example / `ConjectureData` / `ConjectureResult` | test case |
| interesting test case | counterexample |
| `nodes` / `choices` (`ChoiceNode` tuple) | choice sequence |
| shrink target | current best counterexample |
| span (formerly "example" in older versions; the code still names loop variables `ex`) | span |

## 1. What the shrinker operates on

A test case is a tuple of `ChoiceNode(type, value, constraints, was_forced, index)`; the five types are `integer`, `float`, `string`, `bytes`, `boolean` ([choice.py L103-L108](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L103-L108), [data.py L1162-L1168](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L1162-L1168)). Every draw, including a *forced* draw whose value the generator dictated, appends a node; forced nodes carry `was_forced=True` ([data.py L911-L932](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L911-L932)).

A node is *trivial* when it cannot be simplified in isolation: it is forced, or its value equals `choice_from_index(0, ...)` (the simplest value under its constraints); floats use a special rule (0.0 if the interval allows it, otherwise the integer nearest zero inside the interval, otherwise conservatively "not trivial") ([choice.py L132-L170](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L132-L170)). Most passes select only non-trivial nodes.

The shrinker never modifies a forced node ([choice.py L118-L119](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L118-L119)).

## 2. The ordering: shortlex over choice indices

`sort_key(nodes) = (len(nodes), tuple(choice_to_index(node.value, node.constraints) for node in nodes))`. A sequence is simpler if it is shorter, or if equal-length and lexicographically smaller by per-choice index; earlier choices are prioritised because they "potentially get used in more places" ([shrinker.py L73-L94](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L73-L94)). The internals guide states the goal as a *shortlex minimal* choice sequence, approximated rather than found exactly ([guides/internals.rst L42-L57](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/guides/internals.rst#L42-L57)).

`choice_to_index` gives each choice its "complexity index from among its possible values, where 0 is the simplest"; it depends on the constraints, and is meant to be injective (floats currently are not) ([choice.py L325-L337](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L325-L337)).

Per type:

- **integer**: let `a = shrink_towards` clamped into `[min_value, max_value]`. Unbounded: zigzag `[a, a+1, a-1, a+2, a-2, ...]` i.e. `index = 2*|a-x|` minus 1 when `x > a`. Semi-bounded: zigzag until one side is exhausted, then count linearly on the other side (`x - min_value` or `max_value - x`). Bounded: same, with whichever side of `a` is narrower exhausted first ([choice.py L339-L410](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L339-L410), zigzag at [L306-L322](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L306-L322)). Weighted bounded integers with zero weights are unsupported ([L394-L396](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L394-L396)).
- **boolean**: `False` = 0, `True` = 1; if `p` is within 2^-64 of 0 or 1 only one value is possible and its index is 0 ([choice.py L411-L418](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L411-L418)).
- **bytes** and **string**: `collection_index`: order by size first (all sequences of length `k` precede all of length `k+1`, counted from `min_size`), then within a size lexicographically from the left, so `"ab"` is simpler than `"ba"`. Bytes use alphabet 256 with identity element order; strings use `alphabet_size = len(intervals)` and per-character order `intervals.index_from_char_in_shrink_order` ([choice.py L241-L267](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L241-L267), [L419-L435](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L419-L435)). Note that a whole string or byte string is *one* choice; it is not split into per-character choices.
- **float**: `index = (sign << 64) | float_to_lex(|x|)`, so positive precedes negative at equal magnitude ([choice.py L436-L438](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L436-L438)). `float_to_lex` maps a "simple" float (integer-valued, at most 56 bits) to itself, and everything else to a tagged 64-bit encoding: tag bit 1, then an exponent table reordered so positive unbiased exponents come first ascending, then negative descending, with the max exponent (inf/NaN) last; and a mantissa whose fractional bits are bit-reversed so that fewer low-order fraction bits means simpler ([conjecture/floats.py L15-L73](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/floats.py#L15-L73), [L193-L219](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/floats.py#L193-L219)). Net effect: NaN after everything, infinity after all finite values, finite values ordered by integer part then by "lower denominator" of the fraction.

`choices_size` (byte length of the serialised choices) is *not* part of the ordering; it feeds the `BUFFER_SIZE` entropy budget that turns over-long test cases into overruns ([choice.py L634-L637](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L634-L637), [data.py L916-L921](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L916-L921), [engine.py L97-L103](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L97-L103)).

## 3. Spans: which choices belong together

A `Span` marks a region `[start, end)` of the choice sequence that is "logically related": one top-level span for the whole sequence, one per generator draw, and extra spans some generators add (lists mark the "should add another element" choice plus that element as one span). Each span has an opaque integer `label` identifying its origin (roughly, the generator class), a `parent`, `children`, `depth`, `choice_count = end - start`, a `discarded` flag, and optionally a `recorded_value` ([data.py L172-L282](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L172-L282)). Spans are stored as flat integer arrays rather than objects for memory reasons ([L186-L197](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L186-L197)).

`discarded` means the generator's `stop_span(discard=True)` said "the shrinker should be able to delete this span completely without affecting the value", typically after a rejection sampler rejected a value ([data.py L252-L259](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L252-L259), [L1395-L1400](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L1395-L1400)).

The shrinker derives from spans: `spans_starting_at[i]`, `spans_by_label`, `distinct_labels` ([shrinker.py L731-L738](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L731-L738), [L972-L986](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L972-L986)), all cached until the shrink target changes via the `derived_value` decorator ([L284-L299](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L284-L299), [L1172-L1173](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1172-L1173)).

### How collections record their "more" decisions

`lists()` draws through the `many` helper: `while elements.more(): result.append(draw(element))` ([strategies/_internal/collections.py L224-L238](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/strategies/_internal/collections.py#L224-L238)). Each `more()` call opens a span labelled `ONE_FROM_MANY_LABEL` and draws one `boolean` with `p_continue`; the boolean is *forced* to `True` while `count < min_size`, forced to `False` at `max_size`, and not drawn at all when `min_size == max_size`. The span covers the boolean *and* the element drawn after it, and is closed by the next `more()` call; a final `False` ends the collection. When the generator rejects an element (`reject()`), that element's span is closed with `discard=True` so the shrinker can delete it wholesale ([utils.py L262-L361](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/utils.py#L262-L361), label at [L71](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/utils.py#L71)). `invert_many` restates the invariant: variable-size collections draw one continuation boolean before each element and a trailing `False`; fixed-size ones draw none ([utils.py L363-L377](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/utils.py#L363-L377)).

So a list of three integers is the choice sequence `[True, i0, True, i1, True, i2, False]`, with three sibling spans of the same label each covering `[True, ik]`. Nothing in the shrinker knows about lists; it sees booleans, integers and spans.

## 4. When shrinking runs and what counts as a counterexample

### Triggering

After the generate phase the engine calls `shrink_interesting_test_cases` ([engine.py L1656-L1659](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L1656-L1659)). The engine keeps `interesting_test_cases: {interesting_origin -> smallest known result}`. Whenever any test call (including a shrink attempt) is INTERESTING, the engine records it under its origin: a new origin becomes a new entry; an existing origin is replaced if the new result has a smaller `sort_key`, and that counts as a shrink toward `MAX_SHRINKS` ([engine.py L738-L765](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L738-L765)).

`shrink_interesting_test_cases`: set a 300 s deadline; replay every known counterexample and exit as *flaky* if any no longer fails; then, while some origin is unshrunk, pick the unshrunk origin with the smallest `(sort_key(nodes), shortlex(repr(origin)))` and shrink it with predicate `status == INTERESTING and interesting_origin == target`. If `report_multiple_bugs` is off, it instead shrinks the current minimum with predicate `status == INTERESTING` only, "allowing 'slips' to any bug with a smaller minimal test case", and returns after that one shrink ([engine.py L1684-L1733](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L1684-L1733)).

### Acceptance rule inside the shrinker

`Shrinker.cached_test_function(nodes)`:

1. Truncate the candidate to the current length.
2. If the candidate is a prefix of the current target (i.e. identical), return "success" without running.
3. If `sort_key(current) < sort_key(candidate)`, reject without running.
4. If any node's value is not `choice_permitted` under its constraints, reject without running.
5. Otherwise run it through the engine with `extend=0` (max choices = candidate length; drawing past that is an overrun), then `incorporate_test_data`, then `check_calls` (stall budget).

([shrinker.py L394-L416](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L394-L416), engine `extend` semantics at [engine.py L468-L507](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L468-L507).)

`incorporate_test_data` adopts a result as the new target only if `status >= VALID`, the predicate holds, `sort_key(result.nodes) < sort_key(target.nodes)` strictly, and `allow_transition` agrees ([shrinker.py L421-L431](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L421-L431)). Note the comparison is on the nodes the test *actually drew*, not the candidate: a candidate that makes the property stop reading early yields a shorter, hence smaller, sequence.

The engine caches results by choice sequence, and first *simulates* a candidate against the tree of previously seen draws so that known-redundant or known-overrun candidates cost no property execution ([engine.py L483-L553](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L483-L553)).

### Misalignment

When a replayed choice does not fit what the property now asks for (different type, or constraints that do not permit the value), the engine records `misaligned_at` and substitutes the index-0 (simplest) value for that draw, then continues ([data.py L1169-L1212](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L1169-L1212)). The result is still a real test case and can be accepted if it is smaller and still fails.

### A shrink attempt that fails with a different interesting origin

The `Shrinker` is created with the predicate `interesting_origin == target` ([engine.py L1725-L1731](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L1725-L1731)), so an attempt that fails with another origin is *not* adopted as the shrink target: the predicate is false in `incorporate_test_data`. It is not lost either: the engine's `test_function` has already recorded it in `interesting_test_cases` under the other origin (new bug, or a smaller instance of a known one), and cleared that origin from `shrunk_test_cases` so that the outer `while` loop in `shrink_interesting_test_cases` shrinks it afterwards ([engine.py L738-L762](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L738-L762), [L1708-L1733](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L1708-L1733)). The internals guide describes this exactly: "if during shrinking we 'slip' and find a different bug than the one we started with, we will *not* shrink to that, but it will get remembered by the runner" ([guides/internals.rst L145-L152](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/guides/internals.rst#L145-L152)). The `Shrinker` docstring confirms the only predicate in use is "status is INTERESTING and the interesting_origin takes on some fixed value" ([shrinker.py L157-L160](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L157-L160)).

## 5. Scheduling: coarse reduction, then a greedy fixed point

`Shrinker.shrink()` runs `initial_coarse_reduction()` then `greedy_shrink()`, catching `StopShrinking` (the stall budget), and finally the optional explain phase ([shrinker.py L440-L494](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L440-L494)).

`initial_coarse_reduction` runs once, before the main loop, because its transformations "have much more ability to make the test case worse", e.g. re-randomising part of it ([L718-L729](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L718-L729)). It currently consists only of `reduce_each_alternative`.

`greedy_shrink` calls `fixate_shrink_passes(self.shrink_passes)`, which "iterates to a fixed point and so is idempotent" ([L709-L716](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L709-L716)). The initial pass order is:

1. `try_trivial_spans`
2. `node_program("XXXXX")`, `"XXXX"`, `"XXX"`, `"XX"`, `"X"` (delete 5, 4, 3, 2, 1 consecutive nodes)
3. `pass_to_descendant`
4. `reorder_spans`
5. `minimize_duplicated_choices`
6. `minimize_individual_choices`
7. `redistribute_numeric_pairs`
8. `lower_integers_together`
9. `lower_duplicated_characters`
10. `normalize_unicode_chars`
11. `widen_to_span_with_recorded_value`

([shrinker.py L343-L359](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L343-L359)). `remove_discarded` is not in the list; it is run as cleanup between passes.

### `fixate_shrink_passes`

```
while any pass ran a step:
    can_discard = remove_discarded()
    for each pass sp (in current order):
        if can_discard: can_discard = remove_discarded()
        failures = 0
        while failures < 20:
            pad max_stall so a full loop can always complete
            ran = step(sp, random_order = failures >= 10)
            if not ran: break            # this pass's choice tree is exhausted
            if the step made calls: failures = 0 on progress else failures += 1
        record: -1 if sp reduced length, 0 if it shrank without shortening, 1 if nothing
    stable-sort passes by that record
```

Each pass is run until 20 consecutive *steps that made calls* fail to improve the target, which "implicitly boosts shrink passes that are more likely to work". After 10 consecutive failures the step's selection order switches from deterministic to random to escape stalls; a success resumes deterministic order from there. After each pass the pass list is reordered so that length-reducing passes go first next time and useless passes last ([shrinker.py L866-L958](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L866-L958)).

`remove_discarded` runs before every pass while it keeps working; if it fails once it is disabled for the rest of that loop iteration and retried on the next ([L875-L892](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L875-L892)).

The internals guide describes an older scheme of "expensive passes" enabled only after the cheap ones stall ([guides/internals.rst L255-L272](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/guides/internals.rst#L255-L272)); in the current code there is no such flag and the adaptive reordering plus the 20-failure cutoff replaces it (the pass list above is the complete list, [L343-L359](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L343-L359)).

### Steps and the `ChoiceTree`

A pass is a function of a `chooser`. Each `step` executes the pass once; every `chooser.choose(values, condition)` inside it picks one element and records the decision in a per-pass `ChoiceTree`, so that over many steps the pass enumerates every distinct path of decisions exactly once; when the tree is exhausted the pass has nothing left to try until the target changes (the trees are a `derived_value`, so they reset on every successful shrink) ([shrinker.py L832-L864](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L832-L864), [shrinking/choicetree.py L50-L161](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinking/choicetree.py#L50-L161)). Deterministic order resumes from the previous step's prefix and walks *leftwards* (towards lower indices) before wrapping; random order shuffles ([choicetree.py L18-L47](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinking/choicetree.py#L18-L47)).

Design rules for passes, from the module docstring: whether a pass makes progress must be deterministic (so a pass that made no progress will not succeed if rerun immediately, which is what makes the fixed point detectable); passes must not run a constant factor more calls on success than on failure and must not iterate to a fixed point themselves; loops should keep their index valid as the target changes; prefer progress early; be adaptive (bundle successes to turn O(m) into O(log m)); try one or two "special minimal values" first ([shrinker.py L173-L281](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L173-L281)).

## 6. Stopping rule and budget

Three independent limits:

1. **Fixed point.** `fixate_shrink_passes` returns when a full loop over all passes runs no steps, i.e. every pass's choice tree is exhausted against the current target ([shrinker.py L869-L870](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L869-L870), [L926-L933](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L926-L933)). This is the normal exit.
2. **Stall budget, per shrinker.** `max_stall` starts at 200 property calls since the last successful shrink; `check_calls` raises `StopShrinking` after every call once exceeded. It grows: on every successful shrink to `max(max_stall, 2 * calls_since_last_shrink)` ("breathing room" so a shrink that took that long can be found again), and inside the pass loop to `max(max_stall, 2 * max_calls_per_failing_step + calls_since_loop_start)` so a bad pass order cannot stop the run before one full iteration completes ([L334-L341](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L334-L341), [L390-L392](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L390-L392), [L901-L914](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L901-L914), [L1159-L1171](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1159-L1171)). Stopping this way disables the explain phase ([L447-L453](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L447-L453)).
3. **Engine-wide limits.** `MAX_SHRINKS = 500` successful shrinks in total (guards against "exponential (or worse) complexity, where the shrinker appears to be making progress"), and `MAX_SHRINKING_SECONDS = 300` wall-clock for the whole shrink phase, after which the engine prints a warning and exits with the partially shrunk counterexample ([engine.py L79-L95](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L79-L95), [L764-L781](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L764-L781)). These raise out of the engine; the `Shrinker` docstring notes that termination by the engine "is handled by the calling code rather than the Shrinker" ([shrinker.py L168-L171](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L168-L171)).

There is no per-pass call budget beyond the 20-consecutive-failure cutoff.

## 7. The passes

### Pre-pass: `reduce_each_alternative` (coarse reduction, once)

Scans for a non-forced integer node with `min_value == 0` and `value <= 10`, the heuristic signature of a `one_of` branch selector. It sets that node to 0 and checks whether the *shape* changed (different node count, or a later node whose old value no longer fits its new constraints). If the shape changed, it tries each lower branch value `v` in turn with `try_lower_node_as_alternative`: first a plain replacement, then up to three re-randomised completions from the new prefix (`extend=len(nodes)`), splicing the re-randomised span back into the original suffix ([shrinker.py L740-L830](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L740-L830)).

### `try_trivial_spans`

Pick a span; replace every non-forced node inside it with its index-0 value; run. If that did not shrink but produced a valid result, take the same span's contents from that result (which may have realigned) and try substituting those instead ([L1748-L1776](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1748-L1776)). For a list this zeroes every continuation boolean, i.e. empties the list in one call.

### `node_program("X" * n)` for n = 5..1

Delete `n` consecutive nodes starting at a chosen index. If that works, it first uses `find_integer` to slide left to the start of the deletable region, then `find_integer` again to delete the program `k` times from there, so a run of `k` deletable chunks costs O(log k) calls ([L1363-L1400](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1363-L1400), [L1925-L1954](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1925-L1954)). `find_integer(f)` scans 1..4 linearly, then probes exponentially and binary-searches for the largest `n` with `f(n)` true ([junkdrawer.py L435-L470](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/junkdrawer.py#L435-L470)). This is the pass that deletes list elements: for a list of integers each element is the two nodes `[True, i]`, so `"XX"` removes one element and its continuation flag, leaving the sequence well-formed; larger programs remove larger elements or several small ones at once. The pass is span-agnostic; spans are not consulted.

### `remove_discarded` (cleanup, between passes)

While the target has discards, collect all non-overlapping discarded spans with `choice_count > 0`, delete them all in one candidate; return `False` if that candidate is not accepted. Rationale: the `X` programs would find these deletions too, but one block deletion is far cheaper than trying each run individually ([L1312-L1353](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1312-L1353)).

### `pass_to_descendant`

For a label with at least two spans, pick an ancestor span and one of its descendants with the same label and strictly fewer choices, and replace the ancestor's choices with the descendant's. Designed for recursive generators (a binary tree can be replaced by a subtree). Documented as O(len(spans)^2), which is why it sits after the deletion passes ([L988-L1044](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L988-L1044)).

### `reorder_spans`

Pick a span and a label among its children; take all children with that label and run the `Ordering` shrinker over their permutation, with key `sort_key` of each child's node slice, i.e. move simpler children earlier. Example in the docstring: `text(), text()` with `x != y` shrinks reliably to `x="", y="0"` rather than the reverse ([L1878-L1923](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1878-L1923)). `Ordering` first tries a full sort, then adaptively sorts growing contiguous regions from each index (guaranteeing every adjacent swap is tried), then regions with one fixed gap element ([shrinking/ordering.py L16-L96](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinking/ordering.py#L16-L96)).

### `minimize_duplicated_choices`

Group nodes by `(type, choice_key(value))`; pick a group with at least two non-trivial members and minimise them all simultaneously via `minimize_nodes`. Handles `y not in ls` where `ls == [3]` and `y == 3` cannot be lowered separately ([L1355-L1361](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1355-L1361), [L1402-L1429](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1402-L1429)).

### `minimize_individual_choices`

Pick one non-trivial node and run `minimize_nodes([node])`. If that fails and the node is an integer, try the size-dependency fix-up: lower it by one, and if the resulting run has fewer nodes, try that lowered sequence with either one later span or one later single node deleted (the chooser enumerates both options) ([L1778-L1876](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1778-L1876)). This is "the pass that ensures that e.g. each integer we draw is a minimum value" ([L1781-L1787](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1781-L1787)).

`minimize_nodes` dispatches on type ([L1687-L1746](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1687-L1746)):

- integer: try `shrink_towards` directly, then `Integer.shrink` over the distance `|shrink_towards - value|`, once mapping `n -> shrink_towards + n` and once `n -> shrink_towards - n`;
- float: try `|value|`, then `Float.shrink(|value|)` once for the positive and once for the negative sign;
- boolean: the only non-trivial value is `True`; try `False`;
- bytes: `Bytes.shrink` with `min_size`;
- string: `String.shrink` with `intervals` and `min_size`.

All go through `try_shrinking_nodes` (section 8).

### `redistribute_numeric_pairs`

Choose a non-trivial integer or float node `m` and a numeric non-forced node `n` at most 4 positions later; use `find_integer` to move `m` towards its `shrink_towards` by `k` while adding `k` to `n`. Only this direction is tried because raising the earlier node is "strictly worse in the ordering". NaN, infinities and floats beyond `MAX_PRECISE_INTEGER` are excluded ([L1431-L1507](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1431-L1507)). Handles constraints like `m + n > bound`.

### `lower_integers_together`

Choose a non-trivial integer node and a non-forced integer node at most 3 positions later; subtract the same `n` from both, searching with `find_integer` in each direction relative to `shrink_towards`. Comment: it is acceptable to make the later node non-trivial because the ordering is dominated by the earlier node ([L1509-L1540](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1509-L1540)). Handles `abs(m - n) > 1`-style pairs.

### `lower_duplicated_characters`

Choose two non-trivial string nodes at most 4 positions apart that share a character; pick a shared character; run `Integer.shrink` on its shrink-order index, replacing *all* occurrences in both strings with the candidate character ([L1542-L1604](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1542-L1604)).

### `normalize_unicode_chars`

For a string node, for one character position, try each "natural simpler" replacement (NFD/NFKD decomposition characters, upper/lower/casefold) that is in the alphabet and has a smaller shrink-order index. Exists because the binary-search character shrinker "can get stuck on a high codepoint whose simpler equivalents aren't reached by halving / shifting / masking" ([L97-L122](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L97-L122), [L1606-L1640](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1606-L1640)).

### `widen_to_span_with_recorded_value`

Find a span that starts with a non-forced zero-based non-zero integer (a `one_of` selector heuristic), has more than one choice, and has a `recorded_value`; replace the span's choices with a single `ValueHole(value)`. On replay the generator at that position tries to *invert* the value into its simplest encoding across all its alternatives in one execution. Requires generator-side inversion support (`_invert`) ([L1642-L1685](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1642-L1685), [data.py L1148-L1159](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L1148-L1159)).

### Not a shrink pass: `explain`

After shrinking, with the explain phase enabled and if the stall budget did not fire, the shrinker varies each argument span (500 random replacements plus deterministic candidates) to annotate which parts of the counterexample can vary freely. It only adds comments; it never changes the target ([L496-L707](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L496-L707)).

## 8. `try_shrinking_nodes`: the repair step behind value minimisation

Given nodes and a value `n`, replace all of them with `n` and run. Then ([L1175-L1310](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1175-L1310)):

1. If accepted, call `lower_common_node_offset` and return. That helper looks at all nodes that changed since the last check; if several non-trivial integers changed, it subtracts their common minimum distance from `shrink_towards` from all of them at once (searching with `Integer.shrink` in both signs). This breaks the O(m) zig-zag on `abs(m - n) > 1` ([L1046-L1124](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1046-L1124)).
2. If the attempt overran, re-run it with `extend="full"` because lowering a size-controlling choice can make a realigned collection stop failing so that the property draws further (common in stateful tests) ([L1211-L1222](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1211-L1222)).
3. If INVALID, give up.
4. If a string or bytes node in the attempt is now longer than the `max_size` the property asked for (the classic `n = draw_integer(); draw_string(min_size=n, max_size=n)`), retry with that value truncated to fit, keeping either the head or the tail ([L1227-L1264](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1227-L1264)).
5. If the attempt drew fewer nodes than the target (`lost_nodes > 0`), try deleting `lost_nodes` nodes immediately after the replaced region; and for each enclosing span that lost children, also try deleting the region that makes it keep its rightmost children instead of its leftmost. Regions are tried largest first ([L1266-L1310](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1266-L1310)).

The internals guide summarises this as the "adaptive shrink pass" pattern: try a thing, and if it fails inspect what the property did to guess why ([guides/internals.rst L274-L294](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/guides/internals.rst#L274-L294)).

## 9. The per-value shrinkers (`shrinking/`)

All derive from a small `Shrinker` base: `consider(value)` dedupes by canonical form, rejects anything not strictly `left_is_better` than the current value *before* calling the predicate, and adopts the value if the predicate passes; `run` calls `short_circuit` then one `run_step` (or loops to a fixed point if `full=True`, which the main shrinker never sets) ([shrinking/common.py L14-L180](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinking/common.py#L14-L180)). They know nothing about the test API; the main shrinker supplies a predicate that splices the candidate back into the choice sequence ([guides/internals.rst L83-L91](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/guides/internals.rst#L83-L91)).

### `Integer` (non-negative integers)

Guarantees to try `0`, `1`, `initial - 1`, `initial - 2`. `short_circuit`: try 0 and 1; `mask_high_bits` (keep the low `n-k` bits, `k` found adaptively); if more than 8 bits, try `current >> (size-8)` and `current & 0xFF`. `run_step`: `shift_right` (`current >> k`, k via `find_integer`), then subtract multiples of 2, then multiples of 1, each via `find_integer` (exponential probe then binary search) ([shrinking/integer.py L19-L75](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinking/integer.py#L19-L75)). The caller maps the signed problem onto this by shrinking the distance from `shrink_towards` in each direction (section 7, `minimize_nodes`).

### `Float` (non-negative floats; sign handled by the caller)

Ordering is `float_to_lex`. `short_circuit` considers `float_max`, `inf`, `nan` (only accepted if lexically smaller than the current value, so this only matters when starting from NaN or infinity) and stops if the current value is still non-finite. `run_step`: above `MAX_PRECISE_INTEGER` delegate to `Integer` over the float grid (position bijection so `n - 1` is `next_down`); otherwise try rounding to `p` fractional bits for `p` in 0..9 with floor and ceil; if the integer part alone is accepted, delegate to `Integer`; else split as `k + r/n` and minimise `k` with `Integer` ([shrinking/floats.py L46-L124](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinking/floats.py#L46-L124)).

### `Collection`, `Bytes`, `String`

`Collection` orders by length then elementwise by `to_order`. `short_circuit`: try `min_size` copies of the zero element. `run_step`: try all-zero at current length; delete elements from the back, growing the deleted chunk with `find_integer` so a deletable run costs O(log n); `Ordering.shrink` (sort); minimise every duplicated element value simultaneously; then minimise each element in turn with the element shrinker ([shrinking/collection.py L19-L91](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinking/collection.py#L19-L91)). `Bytes` is `Collection` over a list of ints with `Integer` as element shrinker ([bytes.py L15-L23](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinking/bytes.py#L15-L23)); `String` is `Collection` over characters with `to_order`/`from_order` given by the alphabet's shrink order and `Integer` as element shrinker ([string.py L15-L24](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinking/string.py#L15-L24)).

Note the asymmetry: a *string* is one choice and its characters are shrunk by `Collection` inside `minimize_nodes`; a *list* is many choices and its elements are shrunk by the node-level passes (`node_program`, `reorder_spans`, `try_trivial_spans`).

## 10. Summary of special handling per type

- **Integers**: zigzag ordering around a per-draw `shrink_towards`; `Integer` shrinker with 0/1/bit-mask/shift/subtract-multiples strategies via `find_integer`; both directions from `shrink_towards`; `lower_common_node_offset` after success; `redistribute_numeric_pairs` (within 4 nodes) and `lower_integers_together` (within 3 nodes) for coupled pairs; size-dependency fix-ups in `try_shrinking_nodes` and `minimize_individual_choices`; `reduce_each_alternative` and `widen_to_span_with_recorded_value` treat small zero-based integers as branch selectors.
- **Floats**: lexicographic encoding with integer-valued floats simplest; sign shrunk separately (positive first); rounding-to-p-bits, integer delegation, float-grid delegation above 2^53; participate in `redistribute_numeric_pairs`; conservative `trivial` rule.
- **Strings/bytes**: one choice each; size-then-lexicographic ordering with a per-alphabet character order; `Collection` shrinker (delete from back adaptively, sort, dedupe, per-character `Integer`); `lower_duplicated_characters` and `normalize_unicode_chars` passes; truncation repair when a size-controlling integer is lowered.
- **Booleans**: `True -> False` only; as list continuation flags they are what deletion passes remove alongside the element, and what `try_trivial_spans` zeroes to empty a collection.

## 11. Minimal subset for a port

The essentials, in priority order, with the reason each earns its place. The line numbers refer to the sections above.

**Must have (the framework):**

1. `sort_key` shortlex ordering with `choice_to_index` per type (section 2), the acceptance rule (`status >= VALID`, same origin, strictly smaller `sort_key` of the *drawn* nodes, `choice_permitted` pre-check), and running candidates with a hard choice limit so over-long candidates overrun (section 4). Without these nothing else is sound.
2. The greedy fixed-point loop with the 20-consecutive-failure cutoff, the 200-call stall budget with its two growth rules, and an outer cap equivalent to `MAX_SHRINKS`/`MAX_SHRINKING_SECONDS` (sections 5-6). The docstring's determinism invariant for passes is what makes the fixed point detectable ([shrinker.py L181-L194](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L181-L194)). The `ChoiceTree` can be replaced by the "weird loop" index pattern the internals guide documents ([guides/internals.rst L155-L200](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/guides/internals.rst#L155-L200)) at the cost of the random-order stall escape; the adaptive pass reordering is a few lines and worth keeping.
3. `find_integer` ([junkdrawer.py L435-L470](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/junkdrawer.py#L435-L470)). Every adaptive pass and every value shrinker is built on it.

**Core passes (most of the benefit):**

4. `node_program("X"*n)` for n = 1..5 (or at least 1 and 2). Length is the leading term of the ordering, and this is the only general-purpose deletion pass; with the `many` encoding it is also how list elements are removed (section 7). `remove_discarded` is a cheap accelerator only if qc's generators mark discards; the docstring says the `X` passes subsume it ([shrinker.py L1319-L1324](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1319-L1324)).
5. `minimize_individual_choices` with `minimize_nodes`, the `Integer`, `Float` and `Collection`/`String`/`Bytes` shrinkers, and `try_shrinking_nodes` including its lost-nodes region deletion and string truncation repair. This is "the pass that ensures that e.g. each integer we draw is a minimum value" ([L1781-L1787](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1781-L1787)); the repair logic is what makes size-dependent draws (draw `n`, then `n` things) shrink at all ([L1808-L1814](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1808-L1814), [guides/internals.rst L282-L294](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/guides/internals.rst#L282-L294)). A first cut can drop `mask_high_bits` and the float rounding loop, but should keep `shift_right` and `shrink_by_multiples` for O(log n) behaviour on large integers.
6. `try_trivial_spans`. One call per span, tried first, and it collapses whole sub-structures (empties a list, zeroes a record) before any fine-grained work; the docstring's advice to "try one or two special minimal values before anything more fine grained" is this pass ([L278-L280](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L278-L280)). It needs spans, which qc has to record anyway for the next item.
7. `minimize_duplicated_choices`. Same machinery as item 5 applied to a group; cheap and it is what handles `y not in ls`-style coupling ([L1402-L1420](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1402-L1420)).

**Worth adding when cheap:**

8. `reorder_spans` with `Ordering`. Needed for canonical counterexamples when the property is symmetric in its arguments (`x != y`); moderate code size. Requires sibling spans with labels.
9. `redistribute_numeric_pairs` and `lower_integers_together`. Both are about 30 lines on top of `find_integer`, bounded to a 3-4 node window, and unlock sum/difference constraints that otherwise stall or zig-zag ([L1046-L1077](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1046-L1077)). `lower_common_node_offset` covers part of the same ground and comes free with item 5.

**Skip for a minimal port:**

- `pass_to_descendant`: O(spans^2) and only pays off for recursive generators ([L998-L1004](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L998-L1004)); add if qc supports recursion.
- `reduce_each_alternative` and `widen_to_span_with_recorded_value`: both are `one_of` heuristics; the second also needs generator-side value inversion ([L1642-L1657](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1642-L1657)). Plain lexicographic lowering of the selector integer (item 5) still shrinks branch choices when the shape does not change.
- `lower_duplicated_characters`, `normalize_unicode_chars`: Unicode text polish ([L1606-L1619](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1606-L1619)); irrelevant until qc has interval-alphabet strings.
- The explain phase: reporting, not shrinking.

**Interesting-origin handling for qc.** Port the engine-level rule, not just the shrinker's predicate: keep a map from origin to smallest known counterexample, update it on every run (a slip during shrinking is recorded, never adopted), and shrink each origin in turn starting with the smallest (section 4). If qc reports only one bug, Hypothesis's own single-bug mode (predicate = "fails at all", slips allowed) is simpler and always ends at the globally smallest failure it can reach ([engine.py L1719-L1723](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L1719-L1723)).

## Corroborating sources (not authoritative)

- `guides/internals.rst` in the repository, cited inline above: [https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/guides/internals.rst](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/guides/internals.rst).
- `guides/strategies-that-shrink.rst`: shrinking is bottom-up over a labelled tree of choices; the `while draw(more): draw(element)` loop shape is what lets the shrinker delete one iteration without touching anything else, and drawing a size then a collection of that size is the anti-pattern the repair logic exists for ([L93-L139](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/guides/strategies-that-shrink.rst#L93-L139)).
- David R. MacIver and Alastair F. Donaldson, "Test-Case Reduction via Test-Case Generation: Insights From the Hypothesis Reducer", ECOOP 2020, recommended by that guide ([L141-L147](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/guides/strategies-that-shrink.rst#L141-L147)): [https://doi.org/10.4230/LIPIcs.ECOOP.2020.13](https://doi.org/10.4230/LIPIcs.ECOOP.2020.13). Describes the same shortlex-over-choices design; the pass list there predates the typed choice sequence, so the code above is the reference for specifics.
- hypothesis.works articles by MacIver on why shrinking is defined over the generator's choices rather than over values: [Compositional shrinking](https://hypothesis.works/articles/compositional-shrinking/), [How Hypothesis works](https://hypothesis.works/articles/how-hypothesis-works/), [Integrated vs type based shrinking](https://hypothesis.works/articles/integrated-shrinking/). These explain motivation; their implementation details are older than the current code.
