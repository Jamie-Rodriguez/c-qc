# Decide the v1 generator set and signatures

Type: grilling
Status: open
Blocked by: 06, 08

## Question

Given the accepted header shape and the choice model, decide the exact list of v1 draw functions and their signatures: integer widths and range arguments, booleans, floats with NaN and infinity policy, the three string generators (raw bytes, ASCII, UTF-8 by code point with a range), `qc_more` bounds, `qc_one_of` style selection, and how a user writes a generator for their own struct. Decide how each renders in a report.
