# tests/fixtures — SYNTHETIC_TEST_FIXTURE

> **WARNING — SYNTHETIC TEST FIXTURE.**
> Every file in this directory is a **hand-written, synthetic** stand-in for a
> model response. They are **NOT** real Hy3 (or any model) outputs. They exist
> only to exercise the offline evaluation pipeline deterministically in unit
> and end-to-end tests. **They must never be presented as real experiment
> results and must never be written into a real `experiments/` run.**

These fixtures are consumed by `tests/prediction_importer_tests.cpp`,
`tests/reporter_tests.cpp`, and `tests/phase2b_e2e_tests.cpp`. They cover the
required edge cases from the Phase 2B spec:

- empty / whitespace-only response
- non-JSON text
- Markdown-fenced JSON (must NOT be stripped)
- valid JSON followed by trailing text (must NOT be repaired)
- missing key / wrong type / extra key / illegal enum
- `confidence` non-null
- `correct` / `incorrect` / `undetermined` semantic conflicts
- `primary_category` not in `findings`
- duplicate `finding.category` (dedup)
- `implementation_consistency` without candidate (rejected)
- perfect `correct` and perfect `incorrect` predictions
