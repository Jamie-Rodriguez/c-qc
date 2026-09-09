# Document Hypothesis's typed choice sequence

Type: research
Status: resolved
Blocked by: none

## Question

How does Hypothesis currently represent a test case as a sequence of typed choices? Document from the Hypothesis source and its internals documentation: the set of choice types, the constraints each carries, how a draw at the strategy level maps down to choices, how `data.draw` and the `more` decision for collections are expressed in choices, how the sequence is replayed when a prefix is supplied, and how choices are serialised for the example database. Note where the design departed from the earlier raw-byte-stream model and why.

Findings go in `docs/research/hypothesis-typed-choices.md` with a source for every claim.

## Answer

A test case is a flat list of `ChoiceNode {type, value, constraints, was_forced}` over five types: integer (min/max/weights/shrink_towards), float (min/max/allow_nan/smallest_nonzero_magnitude), boolean (p), string (codepoint intervals + size bounds), bytes (size bounds). One ordering, `choice_to_index`/`choice_from_index` (zigzag around shrink_towards, size-then-lexicographic for collections, a lexical 64-bit float encoding), defines "simpler" for the shrinker, the trivial check, and the misalignment fill.
A generator draw is a span around one or more primitive draws; `lists()` uses `many`, which records a boolean "more?" before each element (forced True below min_size, forced False at max_size, absent when min==max) so `[2, 42]` is `[True, 2, True, 42, False]`; a string or bytes value is a single node whose length loop is unobserved.
Replay takes a prefix of values plus `max_choices`: a prefix value of the wrong type or outside the live constraints is a misalignment - the first one is recorded and the index-0 value substituted; an exhausted prefix draws fresh from the PRNG or overruns at `max_choices`; forced draws consume a slot but keep the forced value; a trailing "simplest" template fills the rest with index-0 values.
The database stores values only (tag byte with 5-bit inline length, ULEB128, big-endian ints, binary64, raw bytes, UTF-8); constraints are re-derived by replaying, and decode failure deletes the entry. The same encoding's byte length is the 8 KiB entropy budget.
Bytes were abandoned (issue #3921, 2024-25) for redundancy (many byte strings per value), precision (shrinking through an encoding), and backends; the byte stream survives only as an optional provider beneath the typed sequence.

Full findings: [docs/research/hypothesis-typed-choices.md](../../../docs/research/hypothesis-typed-choices.md)
