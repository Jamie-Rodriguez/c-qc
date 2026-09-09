---
name: coding-standards
description: House style for writing production code. Enforces test-driven development in red-green cycles, failure-mode testing weighted at least as heavily as the happy path, empirical branch coverage plus business-scenario coverage, a post-feature removal check (temporarily delete the new code to prove the new tests fail), semantic plain-English naming that replaces comments (comments only to explain why), the rule of three before abstracting, and minimal indirection. Use this whenever writing or changing any code — implementing a feature, adding a function or endpoint, fixing a bug, refactoring, or reviewing code — even if the user never mentions tests, TDD, or style.
---

# Coding Standards

This skill defines how code gets written here: behavior — including how it fails — is driven out by tests in small red-green cycles, checked for completeness with coverage metrics and business scenarios, proven real by a removal check, and expressed in code that reads like plain English with no more structure than the code currently needs.

## 1. Work test-first, in small red-green cycles

Never write production code without a failing test demanding it. Slice the feature into the smallest behaviors that can each be expressed as one test, then loop:

**Red.** Write one test for the next behavior. Run it and watch it fail. Confirm it fails for the *right reason*: an assertion about the missing behavior (or a not-yet-defined function on the very first test). A test that fails from a typo, a bad import, or broken setup proves nothing — fix that and get a legitimate red before moving on.

**Green.** Write the minimum production code that makes the test pass. Resist implementing ahead of the tests; the next behavior gets its own red first. Run the *whole* suite, not just the new test, so regressions surface immediately.

**Refactor.** Only on green. Improve names, simplify, remove duplication you have now seen three times (see section 8). Run the suite again after.

Repeat until the feature is complete. The discipline matters because watching each test fail first is the only proof that the test *can* fail — a test born green may be asserting nothing.

Bug fixes follow the same loop: first write a test that reproduces the bug (red), then fix it (green).

## 2. Cover every code path — and every business scenario

Red-green drives out behavior one test at a time, but "am I done testing?" needs two complementary answers. Each catches what the other misses.

**Empirical path coverage.** Once the feature's tests are green, run a coverage tool with branch coverage enabled (`pytest --cov --cov-branch`, `go test -cover`, `nyc`, JaCoCo — whatever the stack provides) and inspect the new code. Every line and every branch should be exercised. Prefer branch coverage over line coverage: a file can show 100% line coverage while every `if` was only ever taken one way. Treat each uncovered branch as a missing test and drive it out red-green like any other behavior — never poke code just to move a number. Coverage is a detector of missing tests, not a score to game; 100% coverage with weak assertions is worthless, which is exactly what the removal check (section 4) exposes.

**Scenario coverage beyond the metrics.** Coverage tooling can only see the code that exists; it cannot see the requirements. A single happy-path test can fully cover a function while missing most of what the business cares about. So enumerate scenarios from the requirements, independently of the code:

- boundary values: zero, one, many; empty inputs; min and max; the edges around every threshold (if the credit limit is 100, what happens at exactly 100?)
- combinations of business rules: an expired coupon on a VIP order, a partial refund of a discounted purchase
- every rejection and error path the domain defines (section 3 gives failure modes a full pass of their own)
- ordering and timing cases, where the domain has them

Give each scenario its own plainly named test (`test_coupon_expiring_today_is_still_accepted`). If two scenarios happen to execute the same code path but the business states them as distinct rules, keep both tests: they document both rules and protect against the day the paths diverge.

## 3. Test failure modes as hard as the happy path

How code behaves when things go wrong is specified behavior — often the behavior that matters most, because failures are where data corrupts, charges double, and errors cascade. The natural pull is to drive out the happy path and call the feature done. Resist it: every failure behavior gets the same red-green treatment as every success behavior, and a feature with only happy-path tests is not finished.

For each unit of behavior, drive out at minimum:

- **Invalid input.** Null, empty, out-of-range, malformed — whatever the boundary can actually receive. Decide what should happen (reject with which error? clamp? default?) and pin the decision with a test.
- **Failing dependencies.** The network times out, the API returns a 500, the disk is full, the database is down. This is where passing dependencies in (section 5) pays off: hand the code a stub that raises, and test what it does next.
- **Partial failure.** An operation that dies halfway through a multi-step change. Assert the state it leaves behind: rolled back, retried, or clearly surfaced — never silently half-done.

Assert on the failure *behavior*, not merely that something blew up:

- the specific error type, message, or code the caller will actually see
- that forbidden side effects did not happen — a failed validation must not have charged the card
- that resources were released and state was left consistent

When the requirements are silent about a failure case, that silence is a gap in the requirements, not permission to skip the test. Decide the behavior — or ask the user when the stakes are unclear — then encode the decision as a test, where it becomes documentation.

## 4. The removal check: prove the tests detect the feature

Red-green shows each test could fail once, in isolation; coverage shows the tests reach the code. This final check proves the *completed* test set actually detects the *completed* implementation. Run it last, after sections 2 and 3 are satisfied, so it exercises the full final test set:

1. Get to a clean, green state — full suite passing — and commit the finished work (a WIP commit is fine; you need a restore point).
2. Temporarily remove only the new production code, leaving the new tests untouched:
   - If implementation and tests live in separate files: `git stash push -- path/to/impl_file.py` (one command can list several files), or
   - Replace each new function or method body with a single `raise NotImplementedError` (or the language's equivalent), or delete the new lines outright.
3. Run the full suite and record the results:
   - Every test added for this feature must now fail.
   - Pre-existing tests should still pass, unless the feature deliberately changed shared behavior.
4. If a new test *still passes* with the code gone, it does not detect the feature. Common causes: it asserts against its own mocks instead of real behavior, it re-implements the logic inside the test, or it exercises an old code path. Restore the code, fix the test (watch it go red-green again), and redo the check.
5. Restore exactly: `git stash pop` or revert the edits, then `git diff` against the finished commit to verify the tree is byte-identical, then run the suite once more to confirm green.
6. Report the outcome to the user: list which tests failed while the code was removed. That list is the evidence the feature is genuinely covered.

This is mutation testing in its cheapest form. A test that cannot notice the feature's absence is decoration, and this check is what catches it.

## 5. Design code to be easy to test

Testability is a design outcome, not something bolted on afterward:

- Separate computation from I/O — a functional core of pure logic wrapped in a thin imperative shell that talks to the world. Pure logic is trivially testable; shells stay so thin they barely need tests.
- Prefer pure functions: explicit inputs, returned outputs, no hidden state, no surprise side effects.
- Pass dependencies in as parameters (the clock, randomness, network, filesystem) with sensible production defaults, rather than reaching for globals or singletons. A plain parameter is the flattest form of dependency injection — never build interface hierarchies just for testing (see section 9). It also makes failure injection (section 3) trivial: the test passes a stub that fails on demand.
- Treat test pain as a design signal. If a behavior needs elaborate setup or heavy mocking to test, the code is telling you its dependencies are tangled — fix the design, don't pile on mocks.

## 6. Name things so the code reads like plain English

Apply the read-aloud test: read a line as a sentence, and if it doesn't say what the code means, rename until it does.

- Name variables for what the value *means* in the domain, never for its type or mechanics: `days_until_expiry`, not `d`, `num2`, or `temp`.
- Name functions as verb phrases describing what they do; name predicates as questions (`is_expired`, `has_pending_orders`) so conditionals read naturally: `if coupon.is_expired:`.
- Introduce named intermediate variables instead of nesting clever expressions — an explaining variable is free documentation.
- Avoid abbreviations except truly universal ones (`id`, `url`, `max`).
- Test names state the behavior as a sentence: `test_expired_coupon_is_rejected_at_checkout`.

**Before:**

```python
def proc(d, t):
    r = []
    for x in d:
        if x[1] > t:
            r.append(x[0])
    return r
```

**After:**

```python
def customers_over_credit_limit(customers, credit_limit):
    over_limit = []
    for name, balance in customers:
        if balance > credit_limit:
            over_limit.append(name)
    return over_limit
```

The second version reads aloud as its own specification: for each name and balance in customers, if the balance is over the credit limit, collect the name.

## 7. Write comments only for what code cannot say

Default to zero comments. Names chosen well (section 6) leave a comment nothing to add the vast majority of the time. A comment that describes *what* the code does is a naming failure in disguise — don't write the comment; rename the variable, extract a well-named function, or introduce an explaining variable until the code says it itself.

**Instead of this:**

```python
# check if the user qualifies for the loyalty discount
if u.t == "p" and u.d > 365:
```

**Write this:**

```python
if user.is_premium and user.joined_over_a_year_ago():
```

The one comment worth keeping is the one code cannot express: *why* the code is the way it is. A workaround for a bug in a dependency, the regulation behind a strange-looking rule, the reason a simpler approach was tried and rejected, a link to the incident or spec that motivated the change. That context lives nowhere in the code's behavior and would otherwise be lost:

```python
# Retry once: the payments API returns a spurious 502 on the first
# call after its nightly deploy (vendor ticket PAY-1432).
```

Before writing any comment, ask: is this saying *what* the code does (rename instead) or *why* it does it this way (keep it)?

## 8. Don't abstract until the third occurrence (rule of three)

Duplication is far cheaper than the wrong abstraction.

- First occurrence: just write it.
- Second occurrence: copy it, and make a mental note.
- Third occurrence: *consider* extracting — consider, not automatically do. Extract only if all three sites represent the same underlying concept that should change together. Incidental similarity that will evolve in different directions should stay duplicated.
- Never add parameters, hooks, options, or extension points for hypothetical future needs. The future need, when it arrives, will tell you its actual shape.

## 9. Keep indirection to a minimum

Every layer between the reader and the real logic must pay rent. From any call site, the reader should reach the code that does the actual work in one or two jumps.

Prefer, in order: a plain function → a class → a hierarchy. Concrete smells to remove or refuse to write:

- an interface or abstract base class with exactly one implementation
- a function whose body is a single call to another function with the same arguments
- a factory that can only ever produce one thing
- `Helper` / `Manager` / `Service` classes that hold no state and merely relay calls
- a value threaded through many layers just to reach the one place that uses it

When the rule of three (section 8) does justify extraction, extract the flattest thing that works — usually one well-named function, not a new layer.

## Definition of done

Before reporting a feature complete, confirm:

- Every new behavior was driven by a test that failed first.
- The full suite is green.
- Branch coverage of the new code is complete — every uncovered branch got its own red-green test, not a token execution.
- Business scenarios were enumerated from the requirements — boundaries, rule combinations, rejection paths — and each has a named test, even where the metrics were already satisfied.
- Failure modes are tested as rigorously as successes: invalid inputs, failing dependencies, and partial failures each pin the specified failure behavior — right error surfaced, forbidden side effects prevented, state left consistent.
- The removal check was performed: new code removed, every new test failed, code restored byte-identical, suite green again — and the results were reported.
- The changed code passes the read-aloud test.
- Any comments that remain explain *why*, not *what* — everything a comment could say about what the code does was expressed through names instead.
- No abstraction was introduced before a third occurrence.
- No new layer merely forwards to another.

## Composing with the unit-testing skill

If the `unit-testing` skill is also active, the two divide cleanly by intent. Its source-protection rule ("never modify source to make a test pass without approval") governs writing tests for *existing* behavior, where a failing test may mean the test is wrong. When the user asks for *new* functionality, that request is the approval: the green step's implementation code is exactly the work they asked for. In both cases tests define intended behavior, and source changes only in service of behavior the user requested.
