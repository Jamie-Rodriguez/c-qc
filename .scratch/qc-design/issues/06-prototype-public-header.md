# Prototype the public header

Type: prototype
Status: open
Blocked by: none

## Question

Write a throwaway `qc.h` and two example properties to react to, so the ergonomics can be judged before the engine exists. Show: the `qc_t *` draw-based property signature, explicitly typed draw functions for at least `int32_t`, `bool`, and bytes, `QC_ASSERT` and `QC_ASSUME`, the `qc_more` collection loop, `qc_label`, `qc_check` from a user `main` with `qc_init(argc, argv)`, and `QC_MAIN`. Strict C99, no extensions.

The question this resolves: does the draw-based API read well in real C, and what changes before it becomes the spec? The answer records the accepted shape and the rejected variants.
