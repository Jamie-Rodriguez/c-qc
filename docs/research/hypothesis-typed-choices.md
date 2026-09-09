# How Hypothesis represents a test case as a sequence of typed choices

Research note for ticket `.scratch/qc-design/issues/03-research-hypothesis-typed-choices.md`.
Written 2026-09-09 against Hypothesis `master` at commit
[`cd434f2`](https://github.com/HypothesisWorks/hypothesis/commit/cd434f23be1a3598085cf096e28e6738c63b29b3)
(version 6.168.0, released 2026-09-08). All source links below are permalinks to that commit.

Repository layout note: the Python package no longer lives under `hypothesis-python/`; it is now
`hypothesis/src/hypothesis/...` and the docs are under `hypothesis/docs/`. The contributor guide
`guides/internals.rst` is at the repository root. There is also a new `hypothesis/rust/` crate
(`hypothesis-native`, PyO3) into which parts of the engine are being moved
([Cargo.toml](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/rust/Cargo.toml)).

Vocabulary: this note uses qc's terms from `CONTEXT.md` (property, test case, choice sequence,
generator, shrinking, worker, supervisor). Where Hypothesis's own name differs it is given in
parentheses the first time (Hypothesis says "strategy" for generator, "example" for test case).

Short paths used below:

- `conjecture/` = `hypothesis/src/hypothesis/internal/conjecture/`
- `strategies/` = `hypothesis/src/hypothesis/strategies/_internal/`

---

## 1. The idea in one paragraph

Hypothesis's engine ("Conjecture") represents a test case as the sequence of choices made while
generating it. Each choice is one of five typed primitives (integer, float, boolean, string, bytes)
and corresponds to one of the `draw_*` methods on `ConjectureData`. The typed sequence is called
the *choice sequence*; older code and history call it the "IR". Historically the engine worked
directly on the byte sequence read from the PRNG; it now works on the typed sequence, "which
shrinks far better because we no longer have to reason about the encoding of each value into
bytes." Every choice sequence is one that *could* have been produced by generation, so it is a
valid thing to run; the only ways it can fail to be a valid input are being too short or failing
a precondition, both easily detected.
([guides/internals.rst L16-40](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/guides/internals.rst#L16-L40))

The user-facing glossary gives the canonical example: `True` from `st.booleans()` is the choice
sequence `[True]`; the list `[2, 42]` from `st.lists(st.integers())` is
`[True, 2, True, 42, False]`.
([docs/glossary.rst L81-86](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/docs/glossary.rst#L81-L86))

---

## 2. The choice types and the constraints each carries

Defined in `conjecture/choice.py`:

| Choice type | Python value type | Constraints (TypedDict) | Source |
|---|---|---|---|
| `"integer"` | `int` (arbitrary precision) | `min_value: int \| None`, `max_value: int \| None`, `weights: dict[int, float] \| None`, `shrink_towards: int` | [choice.py L31-35](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L31-L35) |
| `"float"` | `float` (IEEE binary64) | `min_value: float`, `max_value: float`, `allow_nan: bool`, `smallest_nonzero_magnitude: float` | [choice.py L38-42](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L38-L42) |
| `"string"` | `str` | `intervals: IntervalSet` (allowed codepoints), `min_size: int`, `max_size: int` | [choice.py L45-48](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L45-L48) |
| `"bytes"` | `bytes` | `min_size: int`, `max_size: int` | [choice.py L51-53](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L51-L53) |
| `"boolean"` | `bool` | `p: float` (probability of `True`) | [choice.py L56-57](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L56-L57) |

The type aliases `ChoiceT`, `ChoiceConstraintsT` and `ChoiceTypeT = Literal["integer", "string", "boolean", "float", "bytes"]`
are at [choice.py L60-68](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L60-L68).

Semantics of each constraint, from the `PrimitiveProvider` docstrings (the backend contract):

- Integer bounds are inclusive; `None` means unbounded on that side. `weights` maps specific
  keys in `[min_value, max_value]` to a probability. `shrink_towards` "is not used during
  generation and can be ignored by backends."
  ([providers.py L488-515](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/providers.py#L488-L515))
- Float bounds are inclusive; if `allow_nan` is false NaN is invalid; `draw_float` must not
  return `f` with `0 < abs(f) < smallest_nonzero_magnitude`.
  ([providers.py L517-541](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/providers.py#L517-L541))
- String: `intervals` is the set of codepoints to sample; size bounds inclusive.
  ([providers.py L543-563](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/providers.py#L543-L563))
- Bytes: inclusive size bounds.
  ([providers.py L565-580](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/providers.py#L565-L580))
- Boolean: `p` is a hint except at the extremes: `p == 0` must return `False`, `p == 1` must return `True`.
  ([providers.py L467-486](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/providers.py#L467-L486))
- The default `max_size` for strings and bytes is `COLLECTION_DEFAULT_MAX_SIZE = 10**10`
  ("arbitrarily large").
  ([providers.py L78](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/providers.py#L78))

Argument validation at the `ConjectureData.draw_*` layer: integer `weights` require both bounds,
at most 255 entries, `sum(weights) < 1`, and no zero weights; a forced value must lie within the
bounds ([data.py L936-973](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L936-L973)).
Float bounds must not be NaN and `smallest_nonzero_magnitude > 0`
([data.py L975-1017](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L975-L1017)).
An empty `intervals` requires `min_size == 0`
([data.py L1019-1041](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L1019-L1041)).
Booleans: forcing `True` requires `p > 0`, forcing `False` requires `p < 1`
([data.py L1059-1070](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L1059-L1070)).

`choice_permitted(choice, constraints)` is the single predicate for "does this value satisfy these
constraints": integer bounds; float NaN only if allowed, magnitude not in the excluded
`(0, smallest_nonzero_magnitude)` band, and sign-aware bounds (so `-0.0 < 0.0`); string length
and every codepoint in `intervals`; bytes length; boolean forced by `p <= 0` / `p >= 1`.
([choice.py L542-579](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L542-L579))

### 2.1 The node: value + constraints + was_forced

The recorded element of a choice sequence is a `ChoiceNode` with fields `type`, `value`,
`constraints`, `was_forced`, and an `index` (position in the sequence, assigned only when
recorded). `copy()` refuses to change the value of a forced node ("modifying a forced node
doesn't make sense") and deliberately does not copy `index`.
([choice.py L102-130](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L102-L130))

Equality and hashing of nodes use `choice_key`, which maps floats to their bit pattern (so
`-0.0 != 0.0` and NaN payloads are distinguished) and tags booleans so `False != 0`.
([choice.py L172-195](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L172-L195),
[choice.py L586-594](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L586-L594))

Constraint dicts are interned in a 4096-entry LRU (`_pooled_constraints`) keyed by
`choice_constraints_key`, purely to reduce memory pressure.
([data.py L1097-1110](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L1097-L1110),
[choice.py L612-631](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L612-L631))

`was_forced` exists because "Hypothesis occasionally requires that some choices take on a
specific value, for instance to end generation of collection elements early for performance."
([docs/reference/schema_metadata_choices.json](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/docs/reference/schema_metadata_choices.json))

### 2.2 The ordering: `choice_to_index` / `choice_from_index`

Every choice has a *complexity index* among its possible values given its constraints, with 0
the simplest. This one ordering drives the shrinker's sort key, the "trivial" check, and what
gets substituted on misalignment. The functions are meant to be injective inverses of each
other; floats currently are not injective ("nothing has blown up - yet").
([choice.py L325-337](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L325-L337))

- **Integer**: `shrink_towards` is first clamped into `[min_value, max_value]`. Unbounded values
  are ordered by a zigzag around it: `[a, a+1, a-1, a+2, a-2, ...]`. Semi-bounded: zigzag until
  one side is exhausted, then continue on the other side. Bounded: whichever side of
  `shrink_towards` is shorter is exhausted first.
  ([choice.py L306-322](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L306-L322),
  [choice.py L339-410](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L339-L410))
- **Boolean**: `[False, True]`; if `p` is within `2**-64` of 0 or 1 there is only one option, index 0.
  ([choice.py L411-418](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L411-L418))
- **Bytes / string**: `collection_index` counts all sequences shorter than this one (starting
  from `min_size`), then adds the element ranks from the end ("ab" is simpler than "ba").
  Alphabet size is 256 for bytes and `len(intervals)` for strings; string characters are
  ranked by `IntervalSet.index_from_char_in_shrink_order`, which rewires the alphabet so
  `0-9`, then `A-Z`-ish characters come first.
  ([choice.py L198-303](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L198-L303),
  [choice.py L419-435](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L419-L435),
  [intervalsets.py L268-311](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/intervalsets.py#L268-L311))
  `collection_value` raises `ChoiceTooLarge` if the decoded size would be `>= BUFFER_SIZE`;
  callers turn that into an overrun.
  ([choice.py L270-303](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L270-L303))
- **Float**: index is `(sign << 64) | float_to_lex(abs(x))`, and decoding runs the result
  through `make_float_clamper` for the constraints, which is why it is not injective.
  ([choice.py L436-438](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L436-L438),
  [choice.py L526-537](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L526-L537))
  The lexicographic float encoding is a 64-bit tagged union: tag bit 0 means "the low 56 bits
  are a non-negative integer"; tag bit 1 means exponent (re-ordered so positive exponents come
  first in increasing order, then negative in decreasing order) plus mantissa with the
  fractional bits reversed so that low bits shrink first.
  ([conjecture/floats.py L15-73](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/floats.py#L15-L73),
  [L176-219](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/floats.py#L176-L219))
  The clamper returns `f` unchanged if permitted, otherwise resamples inside
  `[min_value, max_value]` using the mantissa bits, then repairs the `smallest_nonzero_magnitude`
  band.
  ([internal/floats.py L34-79](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/floats.py#L34-L79))

`ChoiceNode.trivial` is "forced, or equal to the index-0 value" for non-floats; for floats it
is `0.0` when unbounded, the integer nearest zero inside a finite range, and conservatively
`False` otherwise.
([choice.py L132-170](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L132-L170))

The shrinker's global order is `sort_key(nodes) = (len(nodes), tuple(choice_to_index(...)))`:
shorter first, then lexicographic by index, because earlier choices "potentially get used in
more places".
([shrinker.py L73-94](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L73-L94))

---

## 3. How a generator-level draw maps down to choices

### 3.1 The path from `data.draw(strategy)` to a `ChoiceNode`

`ConjectureData.draw(strategy)` validates the generator, rejects empty ones (`mark_invalid`),
enforces `MAX_DEPTH = 100`, unwraps lazy wrappers to get the label, opens a span with that
label, calls the generator's `do_draw(self)`, records the returned value against the span, and
closes the span.
([data.py L1250-1348](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L1250-L1348),
`MAX_DEPTH` at [data.py L116](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L116))

`do_draw` ultimately calls one of `draw_integer` / `draw_float` / `draw_string` / `draw_bytes` /
`draw_boolean`, each of which builds the (pooled) constraints dict and calls the private `_draw`.
Every `draw_*` accepts `forced=` and `observe=` keyword arguments.
([data.py L936-1070](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L936-L1070))

`_draw(choice_type, constraints, *, observe, forced)` does, in order
([data.py L855-934](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L855-L934)):

1. Overrun if `length == max_length` (the byte budget) or `len(nodes) == max_choices`.
2. If `observe` and a prefix is supplied and not yet exhausted: take the value from the prefix via
   `_pop_choice` (section 5). Otherwise, if not forced, ask the provider:
   `getattr(self.provider, f"draw_{choice_type}")(**constraints)`.
3. If `forced is not None`, the value becomes the forced value (this happens *after* the prefix
   was popped, so a forced draw still consumes one prefix slot).
4. NaN canonicalisation: every NaN is replaced by `int_to_float(float_to_int(value))` so all
   NaNs with the same bits are the same object (issue #3926).
5. If `observe`: notify the `DataObserver` (`draw_<type>(value, constraints=, was_forced=)`),
   compute `size = choices_size([value])` (the serialised byte length, section 6), overrun if
   `length + size > max_length`, append a `ChoiceNode(type, value, constraints, was_forced,
   index=len(nodes))`, and record a CHOICE in the span trail.

`observe=False` draws are "below" the choice sequence: e.g. `draw_string` calls `draw_boolean`
via `many` to decide the character count, and these must not be written to the sequence
"because they are not true choices themselves."
([data.py L796-803](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L796-L803))

The byte budget `BUFFER_SIZE = 8 * 1024` is "the maximum amount of entropy a single test case can
use"; its unit has "no defined semantics" beyond scaling linearly.
([engine.py L97-103](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L97-L103))

### 3.2 Per-generator mapping

| Generator | Choices emitted | Source |
|---|---|---|
| `booleans()` | one `boolean` with `p=0.5` | [misc.py L141-143](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/strategies/_internal/misc.py#L141-L143) |
| `integers(min, max)` | one `integer` with the given bounds; when both bounds exist and the range exceeds 127 it passes `weights={start: 2/128, start+1: 1/128, end-1: 1/128, end: 2/128}` to upweight the endpoints | [numbers.py L71-88](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/strategies/_internal/numbers.py#L71-L88) |
| `floats(...)` | one `float` with `min_value`, `max_value`, `allow_nan`, `smallest_nonzero_magnitude` | [numbers.py L195-201](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/strategies/_internal/numbers.py#L195-L201) |
| `text(alphabet, min_size, max_size)` | one `string` choice when the element generator is the built-in one-character generator; otherwise falls back to a list of one-character `string` draws (each `draw_string(intervals, min_size=1, max_size=1)`) | [strings.py L239-254](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/strategies/_internal/strings.py#L239-L254), [strings.py L200-201](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/strategies/_internal/strings.py#L200-L201) |
| `binary(min_size, max_size)` | one `bytes` choice | [strings.py L459-460](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/strategies/_internal/strings.py#L459-L460) |
| `lists(elements, min_size, max_size)` | `many(...)` (section 4): a `boolean` "more?" before each element, then the element's choices; a final `False` | [collections.py L224-238](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/strategies/_internal/collections.py#L224-L238) |
| `tuples(a, b, ...)` | the element draws in order, each in its own span, no extra choice | [collections.py L75-90](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/strategies/_internal/collections.py#L75-L90) |
| `sampled_from(elements)` | one `integer` in `[0, len-1]`; with a filter, up to 3 rejection-sampled draws, then a "speculative" index draw plus a *forced* index draw | [strategies.py L822-880](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/strategies/_internal/strategies.py#L822-L880) |
| `one_of(a, b, ...)` | `data.draw(SampledFromStrategy(strategies).filter(not-empty))` - i.e. an `integer` branch index - then the chosen branch's choices | [strategies.py L945-957](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/strategies/_internal/strategies.py#L945-L957) |
| `.filter(f)` | up to 3 attempts, each wrapped in a span; rejected attempts close their span with `discard=True`; if all fail, `mark_invalid` | [strategies.py L1410-1435](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/strategies/_internal/strategies.py#L1410-L1435) |
| `data.choice(values)` (internal helper) | `draw_integer(0, len(values)-1)` | [data.py L1461-1475](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L1461-L1475) |

### 3.3 What the default provider does with the constraints

`HypothesisProvider` is the default backend; it holds the `Random` instance and generates fresh
values for choices not supplied by a prefix.
([providers.py L757-763](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/providers.py#L757-L763))

- `draw_boolean(p)`: `False` if `p <= 0`, `True` if `p >= 1`, else `random() < p`.
  ([providers.py L818-829](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/providers.py#L818-L829))
- `draw_integer`: 5% chance of a "constant" (interesting literal mined from the codebase that
  satisfies the constraints); if `weights` is given, an alias-method `Sampler` (with
  `observe=False`, so its internal draws are not recorded) picks either a weighted key or the
  residual mass, which is then drawn from the distribution. Unbounded ranges are sampled as
  `[-2**128, 2**128]`; half-bounded as `[n, max(2**128, 2|n|)]`; the sampling itself goes
  through a piecewise CDF, falling back to uniform when the CDF window is too narrow.
  ([providers.py L831-948](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/providers.py#L831-L948))
- `draw_float`: 15% constants, then 5% "weird floats" (`±0.0`, `±inf`, NaNs, signalling NaNs,
  the bounds and their neighbours) filtered by `choice_permitted`; otherwise
  `lex_to_float(64 random bits)` with a random sign, then clamped.
  ([providers.py L950-1014](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/providers.py#L950-L1014),
  [L1097-1102](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/providers.py#L1097-L1102))
- `draw_string` / `draw_bytes`: constants first; otherwise an *unobserved* `many` loop with
  `average_size = min(max(2*min_size, min_size+5), 0.5*(min_size+max_size))` picks the length,
  and each character is a uniform index into the alphabet (for alphabets larger than 256, 80% of
  characters come from the first 256). The whole string or bytes value is a single choice.
  ([providers.py L1016-1095](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/providers.py#L1016-L1095))

`BytestringProvider` is the other in-tree provider: it parses a caller-supplied byte string into
the five choice types (used for `fuzz_one_input`), which shows the byte stream survives only as
one possible *source* beneath the typed sequence, not as the representation itself.
([providers.py L1111-1241](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/providers.py#L1111-L1241))

---

## 4. The "more" decision for collections: `many`

`conjecture/utils.py::many` "bundles up the logic we use for 'should I keep drawing more
values?' and handles starting and stopping spans in the right place."
([utils.py L262-272](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/utils.py#L262-L272))

Construction computes `p_continue = _calc_p_continue(average_size - min_size, max_size - min_size)`.
The base formula is `p = 1 - 1/(1 + desired_avg)`, corrected for small `max_size` by a short
descent and then a binary search so that the expected size is within 0.01 of the target.
([utils.py L274-296](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/utils.py#L274-L296),
[utils.py L383-426](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/utils.py#L383-L426))

`more()` ([utils.py L306-345](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/utils.py#L306-L345)):

1. Closes the previous element's span (with `discard=True` if that element was rejected).
2. Opens a new span labelled `ONE_FROM_MANY_LABEL` ("one more from many()").
3. If `min_size == max_size`, **no choice is drawn at all**: continue iff `count < min_size`.
4. Otherwise it draws `draw_boolean(p_continue, forced=...)` where the forced value is `True`
   while `count < min_size`, `False` when `count >= max_size` or a forced stop is pending, and
   `count < forced_size` when a total size was forced. The boolean is a *real, recorded* choice
   even when forced (`was_forced=True`).
5. On `True`, `count += 1` and the caller draws the element inside the open span. On `False` the
   span is closed and iteration ends.

`reject()` un-counts the last element and, after more than `max(3, 2*count)` rejections, either
`mark_invalid`s the test case (if still below `min_size`) or sets `force_stop` so the next
`more()` is forced `False`.
([utils.py L347-360](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/utils.py#L347-L360))
Since 6.167.1 a rejected element's span is marked discarded "so the shrinker can delete them
wholesale and generation avoids revisiting choices that would be rejected again."
([changelog v6.167.1](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/docs/changelog.rst#L42-L50))

`invert_many` states the resulting shape of the sequence explicitly: "Fixed-size collections draw
no booleans at all; variable-size ones draw a continuation boolean before each element (forced
while below min_size, but forced draws still consume a choice) and a final False to stop (forced
at max_size)."
([utils.py L363-377](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/utils.py#L363-L377))

So `[2, 42]` from `lists(integers())` is `[True, 2, True, 42, False]`
([glossary](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/docs/glossary.rst#L84)),
with spans: the list's span, and inside it three `ONE_FROM_MANY` spans covering `[True, 2]`,
`[True, 42]`, `[False]`, each element draw in its own nested span.

### 4.1 Spans

A `Span` marks a region of the choice sequence that is logically related: the top-level span, one
per generator draw, and extra ones such as `lists()`'s "should add another element" spans. Spans
are stored as indices into compact integer lists rather than objects.
([data.py L172-199](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L172-L199))
`SpanRecord` keeps a flat `trail` of ints: `STOP_SPAN_DISCARD=1`, `STOP_SPAN_NO_DISCARD=2`,
`CHOICE=3`, and `4 + label_index` for a span start.
([data.py L340-391](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L340-L391))
Labels are 64-bit values from `sha384(name)[:8]`, combined for composite generators by
shift-xor.
([utils.py L32-67](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/utils.py#L32-L67))
Closing a span with `discard=True` also calls `observer.kill_branch()`, pruning that prefix from
future novel-prefix generation.
([data.py L1395-1438](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L1395-L1438))
The backend-facing description of spans, including the guarantee that starts and ends are always
balanced and that a span may contain zero choices, is in `PrimitiveProvider.span_start`.
([providers.py L687-755](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/providers.py#L687-L755))

---

## 5. Replay: supplying a prefix of choices

### 5.1 Constructing a replay

`ConjectureData(prefix=..., max_choices=..., random=...)` takes a prefix that is a sequence of
raw values (`ChoiceT`), `ChoiceTemplate`s, or `ValueHole`s.
([data.py L682-689](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L682-L689))
`ConjectureData.for_choices(choices)` sets `max_choices = choice_count(choices)`, i.e. an exact
replay that overruns if the property draws past the end.
([data.py L664-680](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L664-L680),
[engine.py L238-252](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L238-L252))
The prefix holds *values only*; the constraints come from the generators as they run, which is
what makes misalignment detectable.

The engine's `cached_test_function(choices, extend=...)` chooses `max_choices`: `extend=0`
(default) means exact; `extend=n` allows `n` extra fresh choices; `extend="full"` removes the
choice-count limit (leaving only the byte budget).
([engine.py L468-553](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L468-L553))
Replays from the example database use `extend="full"`
([engine.py L1090-1098](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L1090-L1098)),
and so does the shrinker's repair path when an attempt overruns
([shrinker.py L1211-1224](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1211-L1224)).

### 5.2 What happens at each draw during replay

From `_draw` and `_pop_choice`
([data.py L855-934](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L855-L934),
[data.py L1112-1212](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L1112-L1212)):

| Situation at a `draw_*` call | Behaviour |
|---|---|
| `len(nodes) == max_choices` or byte budget hit | `mark_overrun()` - test case status `OVERRUN`, before consulting the prefix |
| Prefix has a plain value; its Python type matches the requested choice type **and** `choice_permitted(value, constraints)` | value is used; prefix index advances |
| Prefix value is of a different choice type, or not permitted by the constraints | **misalignment**: `misaligned_at = (index, choice_type, constraints, forced)` is recorded (first misalignment only); the value is replaced by `choice_from_index(0, choice_type, constraints)` (the simplest permitted value); prefix index advances; the test continues |
| `choice_from_index(0, ...)` raises `ChoiceTooLarge` | `mark_overrun()` |
| The draw is `forced=` | the prefix slot is still consumed, but the forced value wins |
| Prefix is exhausted and `len(nodes) < max_choices` | provider generates a fresh value (needs a `random`; `for_choices` sets `max_choices` so this never happens there) |
| Prefix element is `ChoiceTemplate("simplest", count)` | must be the last prefix element; every subsequent draw yields the index-0 value; if `count` is set, it is decremented and going below zero overruns |
| Prefix element is an unclaimed `ValueHole` | treated as a misalignment with the index-0 value |

The rationale for filling misalignments with index 0 rather than a "similarly complex" value is
spelled out in the code: attaching an index to every prefix choice would complicate the API and
is not always available (e.g. when reading from the database); high-complexity slips are believed
to be rare.
([data.py L1186-1212](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L1186-L1212))
The motivating example is `one_of(integers(0, 100), integers(101, 200))`: the sequence `[0, 100]`
carries constraints `{0..100}` at index 1, but `[1, 100]` would meet `{101..200}` there, which
permits none of 0-100.
([data.py L1169-1184](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L1169-L1184))

`misaligned_at` is carried into the frozen `ConjectureResult`
([data.py L631-659](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L631-L659))
and the runner counts misaligned test cases for statistics
([engine.py L659-662](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L659-L662)).

Statuses are `OVERRUN < INVALID < VALID < INTERESTING`; `conclude_test` freezes the data and
raises `StopTest` to unwind out of the property.
([data.py L121-125](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L121-L125),
[data.py L1477-1500](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L1477-L1500))

### 5.3 How the engine uses prefixes

- **Generation** = novel prefix + random tail. `DataTree.generate_novel_prefix` walks the radix
  tree of every previously seen sequence and stops as soon as it produces a value not seen at that
  position; the rest of the test case is generated fresh by the provider.
  ([datatree.py L702-812](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/datatree.py#L702-L812),
  [engine.py L808-817](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L808-L817))
  The tree stores `(choice_type, constraints, value)` per node plus which were forced, and
  `compute_max_children(choice_type, constraints)` tells it when a node is exhausted
  (e.g. bounded integers: `max - min + 1`; booleans: 1 or 2; unbounded integers: `2**128 - 1`).
  ([datatree.py L330-420](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/datatree.py#L330-L420),
  [datatree.py L194-279](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/datatree.py#L194-L279))
- **The "zero" test case** is `cached_test_function((ChoiceTemplate("simplest", count=None),))`,
  and a health check fires if it overruns or uses more than half the byte budget.
  ([engine.py L1200-1235](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L1200-L1235))
  Early in generation, `prefix + (ChoiceTemplate("simplest"),)` measures the minimal extension of
  a prefix and caps `max_length = len(prefix) + 5 * minimal_extension`.
  ([engine.py L1306-1330](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L1306-L1330))
- **Prediction before execution**: `DataTree.simulate_test_function` replays a prefix against the
  recorded tree and raises `PreviouslyUnseenBehaviour` if it reaches unknown territory; the
  runner only calls the real property when the outcome cannot be predicted. The result cache is
  keyed by `choices_key(values)` - values only, not constraints.
  ([datatree.py L826-872](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/datatree.py#L826-L872),
  [engine.py L460-466](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L460-L466))
- **Flakiness detection** is a by-product: when a replay with the same values draws a different
  type or constraints than the tree recorded, or draws more data after a recorded conclusion, or
  forces a value that was not forced before, `TreeRecordingObserver` raises
  `FlakyStrategyDefinition` ("Inconsistent data generation!").
  ([datatree.py L1026-1126](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/datatree.py#L1026-L1126),
  [errors.py L111-160](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/errors.py#L111-L160))
- **Shrinking** replaces node values and replays. When lowering a node that controlled a later
  collection's size, the replayed collection gets realigned to the simplest value; the shrinker
  detects a string/bytes node whose recorded value is now longer than the new `max_size` and
  retries with it truncated from either end, and otherwise tries deleting the region of "lost"
  nodes after the change.
  ([shrinker.py L1175-1300](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/shrinker.py#L1175-L1300),
  [guides/internals.rst L282-294](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/guides/internals.rst#L282-L294))

### 5.4 `ValueHole` (2026 addition, for completeness)

A `ValueHole` carries a *value* rather than choices. When `ConjectureData.draw` finds one at the
current prefix position it asks the generator being drawn to `_invert(value)` into a choice
sequence and splices the result into the prefix; if inversion fails, the hole is treated as a
misalignment. Inversion is best-effort and must be pure (drawing during `_invert` raises
`CannotInvert`).
([choice.py L84-99](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L84-L99),
[data.py L1288-1309](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/data.py#L1288-L1309),
[strategies.py L592-607](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/strategies/_internal/strategies.py#L592-L607))
It exists so that in `wide | specific`, a value from the specific branch can be re-encoded and
shrunk as if generated by the wider branch.
([changelog v6.165.1](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/docs/changelog.rst#L203-L214),
[v6.165.2](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/docs/changelog.rst#L184-L200))

---

## 6. Serialisation for the example database

`hypothesis/database.py::choices_to_bytes` / `choices_from_bytes`. The comment explains the
custom format: "our data is a flat sequence of elements, and standard tools like protobuf or
msgpack don't deal well with e.g. nonstandard bit-pattern-NaNs, or invalid-utf8 unicode."
([database.py L1267-1304](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/database.py#L1267-L1304))

Per element, one metadata byte `tag_ssss` (3-bit tag in the high bits, 5-bit size in the low
bits), then optionally a ULEB128 size, then the payload:

| Tag | Type | Payload |
|---|---|---|
| 0 | boolean | none - the low bit of the metadata byte is the value (`b"\1"` / `b"\0"`) |
| 1 | float | 8 bytes, `struct.pack("!d")` (big-endian binary64; NaN bit patterns preserved) |
| 2 | integer | big-endian two's complement, `1 + bit_length // 8` bytes |
| 3 | bytes | raw |
| 4 | string | UTF-8 with `errors="surrogatepass"` |

If the payload size is `< 31` it is stored in the 5 low bits; otherwise those bits are `0b11111`
and a ULEB128 length follows.
([database.py L1228-1265](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/database.py#L1228-L1265),
[L1307-1335](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/database.py#L1307-L1335))

Only **values** are stored; constraints and `was_forced` are not. `choices_from_bytes` returns
`None` on any decoding error ("eg because our format changed or someone put junk data in the
db"), and the runner deletes such entries.
([database.py L1338-1350](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/database.py#L1338-L1350),
[engine.py L1090-1094](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L1090-L1094))

The same encoding defines the entropy budget: `choices_size(choices) = len(choices_to_bytes(choices))`
is what `_draw` adds to `length` and compares against `BUFFER_SIZE`.
([choice.py L634-637](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/choice.py#L634-L637))

`save_choices` writes `choices_to_bytes(choices)` under the property's database key (plus
sub-keys such as `pareto`); a failing test case's choices are saved even when the failure is an
exception that escapes the runner.
([engine.py L946-957](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L946-L957),
[engine.py L609-628](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/src/hypothesis/internal/conjecture/engine.py#L609-L628))

This format shipped in 6.124.0 (2025-01-16): "The Hypothesis example database now uses a new
internal format to store examples. This new format is not compatible with the previous format."
([changelog v6.124.0](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/docs/changelog.rst#L2891-L2898))

For tooling, the observability output exposes each node as `{type, value, constraints, was_forced}`
with NaNs as `["float", bits]`, integers `>= 2**63` as strings, and bytes base64-encoded, plus
spans as `[label, start, end, discarded]`.
([schema_metadata_choices.json](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/docs/reference/schema_metadata_choices.json),
[changelog v6.135.3](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/docs/changelog.rst#L2055-L2062))

---

## 7. Why the design moved off raw bytes

The migration is tracked in issue #3921, "Migrate our core representation to the typed choice
sequence", opened by Liam DeVoe (tybug). Its own summary of the old model: generation viewed
strategies as a parser of a bytestring, the database stored bytestrings, shrinking worked on the
bytestring ("internal shrinking"), and novel inputs were novel byte prefixes. The stated
shortcomings:

- **Redundancy.** "The mapping of bytes -> input is not injective, so an input may have many byte
  representations. For instance, `0` is represented by many different bytestrings, so any strategy
  using `st.integers()` effectively wastes some number of inputs."
- **Precision.** "Effecting predictable changes in the input via changes in the bytestring is
  difficult, in e.g. shrinking." Float shrinking was a hack that parsed bytes that "look like they
  *could* represent floats", shrank the float, and re-serialised it.
- **Backends.** For CrossHair/SMT integration "bytes is simply too low level to get an efficient
  SMT algorithm out of."

The fix "lift[s] up the representation from bytes to a slightly higher representation at the
level of five types", improving redundancy "as `DataTree` operates at this higher level" and
precision "as we retain type and shape information about what was previously spans of the
bytestring."
([issue #3921](https://github.com/HypothesisWorks/hypothesis/issues/3921))

The internals guide states the shrinking consequence directly: the typed sequence "shrinks far
better because we no longer have to reason about the encoding of each value into bytes", and
floats, though drawn as a single choice, still get a dedicated shrinker that knows the lexical
encoding.
([guides/internals.rst L30-33, L65-69](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/guides/internals.rst#L30-L69))

Milestones from the changelog:

- 4.25.0 (2019-07-03): the user-visible `buffer_size` setting of the byte era is deprecated and disabled.
  ([changelog v4.25.0](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/docs/changelog.rst#L11064-L11070))
- 6.103.0 (2024-05-29): shrinker migrated to "the IR layer"; median 1.38x faster on the Hypothesis suite.
  ([changelog v6.103.0](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/docs/changelog.rst#L3786-L3794))
- 6.112.0 (2024-09-05): variable-width bytes in the IR (`draw_bytes(min_size, max_size)`), a breaking change for backends.
  ([changelog v6.112.0](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/docs/changelog.rst#L3444-L3449))
- 6.115.2 (2024-10-14): endpoint upweighting for `integers()` folded into the `weights` constraint.
  ([changelog v6.115.2](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/docs/changelog.rst#L3347-L3353))
- 6.118.x (2024-11): targeted PBT, the explain phase and novel-input generation migrated; previously-shrunk database entries are no longer re-shrunk.
  ([changelog v6.118.4](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/docs/changelog.rst#L3244-L3250),
  [v6.118.2](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/docs/changelog.rst#L3260-L3266),
  [v6.118.6](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/docs/changelog.rst#L3223-L3229))
- 6.123.2 (2024-12-27): the shrinker orders failing test cases by the typed sequence, so reported minimal counterexamples may change.
  ([changelog v6.123.2](https://github.com/HypothesisWorks/hypothesis/blob/cd434f23be1a3598085cf096e28e6738c63b29b3/hypothesis/docs/changelog.rst#L3023-L3029))
- 6.124.0 (2025-01-16): database switched to the choice-sequence format (section 6).

---

## 8. Implications for a C99 port

These are suggestions derived from the above; they respect ADR-0001 (supervisor/worker) and
ADR-0002 (choices stream to the supervisor as drawn).

1. **Make the choice node the unit of everything.** Define one tagged struct,
   `qc_choice { type; value; constraints; was_forced; }`, with `constraints` a tagged union
   mirroring section 2. Generation, replay, the shrinker, the span trail, the cache key and the
   serialiser all operate on arrays of these. The worker streams *nodes* (type, value,
   constraints, was_forced) to the supervisor, not just values: the shrinker needs constraints to
   compute indices and detect misalignment, and the database does not store them (section 6), so
   the supervisor cannot recover them any other way except by replaying.

2. **Keep the five types, but decide the integer and string representations up front.**
   - Integer: Hypothesis uses arbitrary precision and samples "unbounded" as `[-2^128, 2^128]`.
     In C, define unbounded as the full `int64_t` range and do zigzag/index arithmetic in
     `uint64_t` (the distance `max - min` can overflow `int64_t`). The index of a bounded integer
     fits in `uint64_t` for any `int64_t` range. Keep `shrink_towards`; make `weights` optional
     (Hypothesis only uses it for endpoint upweighting and it is ignored by the ordering).
   - String: the `IntervalSet` alphabet is the only non-scalar constraint. Options: (a) drop the
     string type and let text generators be `bytes` plus a codepoint alphabet table, or (b) keep
     a string type whose constraint is an array of `(lo, hi)` codepoint ranges plus size bounds.
     Either way, implement `collection_index`/`collection_value` once, parameterised by
     `alphabet_size` and an element rank function, exactly as `choice.py` does.
   - Float: port the lexical encoding (`float_to_lex`/`lex_to_float`: exponent permutation table,
     mantissa bit reversal, 56-bit "simple integer" branch), `sign_aware_lte` and
     `make_float_clamper`. They are pure bit manipulation and translate to C directly.

3. **Implement `choice_to_index` / `choice_from_index` / `choice_permitted` first** and derive
   the shrinker sort key, `trivial`, and the misalignment fill from them. This is the single
   definition of "simpler" in Hypothesis and should be in qc too.

4. **Collections are boolean-per-element, with forced continuation.** Port `many` exactly:
   `p_continue` from `average_size`, forced `True` below `min_size`, forced `False` at
   `max_size`, no boolean at all when `min_size == max_size`, one span per element, discard on
   rejection. The "more?" boolean must be a recorded choice so the shrinker can delete
   `[True, element...]` pairs. By contrast, the length loop inside `draw_bytes`/`draw_string`
   is *unobserved*: a bytes choice is one node whose value carries its own length.

5. **Replay semantics for the worker.** A worker runs a test case from `(prefix, max_choices,
   seed)`. Per draw: budget check (node count and serialised-byte budget of 8 KiB, or qc's
   equivalent) -> pop prefix -> type/permitted check -> on mismatch record the first
   `misaligned_at = (index, type, constraints)` and substitute the index-0 value -> forced
   overrides -> record node and stream it. When the prefix is exhausted and there is room, draw
   fresh from the PRNG. Report `OVERRUN`/`INVALID`/`VALID`/`INTERESTING` plus `misaligned_at`
   back to the supervisor. Support a trailing "simplest" template so the supervisor can ask for
   the zero test case and for minimal extensions of a prefix.

6. **Wire protocol = span trail + nodes.** `SpanRecord`'s flat trail (`CHOICE`, `STOP`,
   `STOP_DISCARD`, `START(label_index)`) is already a byte-oriented event stream; the worker can
   emit exactly these events interleaved with node records over the pipe from ADR-0002. Labels
   can be 64-bit hashes of the generator name (Hypothesis uses `sha384(name)[:8]`; any stable
   64-bit hash works) combined by shift-xor for composite generators.

7. **Adopt `choices_to_bytes` verbatim for the on-disk database.** It is ~40 lines in C: a tag
   byte with 5-bit inline length, ULEB128 for longer payloads, big-endian two's complement
   integers, big-endian binary64, raw bytes. Store values only; re-derive constraints by replay;
   treat any decode failure as "delete this entry". Reuse the same encoding as the entropy budget
   measure, as Hypothesis does.

8. **Forced draws are part of the primitive API.** `qc_draw_integer(..., forced)` etc. are needed
   by `many` and by filtered `sampled_from`; the node must carry `was_forced`, and the shrinker
   must never rewrite a forced node's value.

9. **Cache and equality use bit patterns.** Key the result cache and node equality on the float
   bit pattern (`-0.0 != 0.0`, NaN payloads distinct) and on a type tag so `false != 0`.
   Canonicalise NaNs on draw as Hypothesis does if values are ever compared for uniqueness.

10. **What can be deferred.** `DataTree` (novel-prefix generation, exhaustion detection,
    prediction-before-execution and flakiness detection) is a large optimisation that sits behind
    the `DataObserver` hook; qc can start with pure random generation and add it later without
    changing the node format. `ValueHole`/`_invert`, the mined-constants pool, the weighted
    integer sampler and the piecewise integer distribution are likewise independent of the
    representation.
