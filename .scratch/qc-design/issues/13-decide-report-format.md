# Decide the failure report format and counterexample rendering

Type: grilling
Status: open
Blocked by: 09, 11

## Question

Decide what the user sees when a property fails: how the minimal counterexample is rendered from the choice sequence using labels and generator-specific formatting, how each failure kind is presented (assert with message and location, signal or exception with fault address, abort, exit, timeout, sanitizer), where captured stderr goes, how the reproduction information is shown, and the summary line per property and per run.
