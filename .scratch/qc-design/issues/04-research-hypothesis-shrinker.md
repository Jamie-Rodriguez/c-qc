# Document Hypothesis's shrinker

Type: research
Status: resolved
Blocked by: none

## Question

How does Hypothesis's shrinker work on the typed choice sequence? Document from source: the ordering it minimises toward, the shrink passes and the order they run in, how it decides when to stop, how it handles a test case that fails differently from the original (the "interesting origin"), how it treats the `more` decisions of collections, and the special handling for integers, floats, and strings. Identify the smallest subset of passes that gives most of the benefit, for a minimal port.

Findings go in `docs/research/hypothesis-shrinker.md` with a source for every claim.

## Answer

Hypothesis shrinks toward the shortlex-minimal choice sequence: fewer choices first, then lexicographically smaller per-choice `choice_to_index` (integers zigzag around `shrink_towards`, booleans False<True, strings/bytes by size then left-to-right character order, floats by a lex encoding where integer-valued floats are simplest). A candidate is accepted only if it runs to a valid result, fails with the *same* interesting origin, and its actually-drawn sequence is strictly smaller; a slip to a different origin is never adopted but the engine records it and shrinks it afterwards.
Scheduling is a greedy fixed-point loop over ~11 passes, each run until 20 consecutive failed steps, with passes re-sorted after each loop (length reducers first); it stops at the fixed point, after 200 property calls with no shrink (a budget that grows with progress), or at engine caps of 500 shrinks / 300 s.
Lists are encoded as `[True, elem, True, elem, ..., False]` with one span per continuation+element, so element deletion is just the generic `node_program("X"*n)` contiguous-node deletion pass; strings and bytes are single choices shrunk by a Collection shrinker. Integers use `find_integer` (linear 1..4, exponential probe, binary search) via the `Integer` shrinker in both directions from `shrink_towards`, plus pair passes (`redistribute_numeric_pairs`, `lower_integers_together`) and size-dependency repair in `try_shrinking_nodes`.
Minimal port: shortlex ordering + acceptance rule + fixed-point loop with stall budget + `find_integer`; passes `node_program X1..X5`, `minimize_individual_choices` (with `Integer`/`Float`/`Collection` shrinkers and the lost-nodes repair), `try_trivial_spans`, `minimize_duplicated_choices`; then `reorder_spans` and the two integer-pair passes when cheap. Skip `pass_to_descendant`, the `one_of` heuristics, the Unicode passes, and explain.

Full findings: [docs/research/hypothesis-shrinker.md](../../../docs/research/hypothesis-shrinker.md)
