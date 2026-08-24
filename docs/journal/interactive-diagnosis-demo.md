# Interactive greedy diagnosis demo journal

Date: 2026-08-24  
Branch: `codex/interactive-diagnosis-ui`  
Base: Phase 2C sanitized closure `82997a7`

## Scope and separation

This milestone turns the existing Hy3 diagnosis path into a local Chinese web
demo. It remains separate from the frozen evaluation pipeline: no gold,
reference verdict, dataset manifest, or review metadata enters an interactive
request, and no interactive result enters Phase 2C metrics or `data/`.

Optional C++17 source is model context for static review only. The demo does not
compile or run code, contact an online judge, or report observed CE/RE/TLE/WA.

## Implementation

- `InteractiveDiagnosisRequest` strictly validates the fixed `greedy` type,
  required problem/reasoning fields, optional source/tests/notes, UTF-8, unknown
  keys, field sizes, and safe request identifiers.
- `hy3-interactive-diagnosis-v1` is an independent, label-free prompt. Its
  normalized template SHA-256 is
  `f1a0b0e5dfdc854efed55becadc9d7b45051794786ad87f0e77c74297a1b1c3a`.
- The existing `Hy3ModelClient`, `ProductionHttpTransport`, and shared
  `ModelRunner::invokeModelOnce` boundary perform one non-retrying call.
- A request directory is atomically latched before invocation. The local,
  Git-ignored audit stores the rendered prompt, attempting/final sidecar,
  verbatim raw response, hashes, and strict diagnosis. Browser JSON contains
  only the parsed diagnosis and safe metadata.
- The loopback HTTP service provides `GET /api/health`, `POST /api/diagnose`,
  and three static UI assets. It enforces loopback binding/Host values, a
  256-KiB request-body ceiling, strict content type, no CORS, restrictive
  browser headers, and at most one active diagnosis.
- The page maps all seven categories to Chinese, groups findings by six stages,
  separates transport errors from diagnosis errors, disables duplicate clicks,
  preserves input after failure, and never stores or receives the API key.

The HTTP server dependency is vendored `cpp-httplib v0.51.0` (`d66d9a9`), MIT.
The upstream header SHA-256 is
`dfbaccb76432ed6d56ddd9983fd9d262b61ba6ba0958f6b00db35c802607bd35`;
the preserved license SHA-256 is
`4b45cbe16d7b71b89ae6127e26e0d90a029198ca5e958ad8e3d0b8bbed364d8b`.

## Focused verification

- MSVC C++17 `/W4 /utf-8`: interactive business tests `18/18` passed.
- MSVC C++17 `/W4 /utf-8`: HTTP application tests `10/10` passed.
- FakeModelClient covers parsed, undetermined, fenced invalid JSON, semantic
  invalidity without code, authentication redaction, immutable request latch,
  raw/hash/sidecar linkage, and no second call.
- Frontend: `node --check` passed; all referenced DOM IDs and seven taxonomy
  mappings passed a static consistency check.
- Offline loopback smoke: page `200`, health `200`, malformed JSON `400`;
  health did not probe TokenHub.
- Automated in-app visual inspection was blocked by the local browser security
  policy before navigation. No alternate browser bypass was attempted.

## Single real smoke

Exactly one POST used the built-in CF 160A example; there was no retry.

- HTTP: `200`
- outcome / parse status: `diagnosed` / `parsed`
- model / model version: `hy3` / `null`
- latency: `2212 ms`
- tokens: prompt `1045`, completion `170`, total `1215`
- prompt SHA-256:
  `983021890b3993d28d78ccf6a3f6670c93a4b27cef6bd726585ea6899c472d9f`
- raw response SHA-256:
  `4bbfd8d57a8d0c233abd5855b4beea5b4ea08bff59fd3dd7732a900c2abf6102`
- parsed result: `correct`, primary category `null`

The transport/audit/strict-parser path succeeded, but the model result was a
quality failure for the intended example. Hy3 incorrectly stated that
`taken*2 >= total` is equivalent to the required strict inequality and missed
the implementation mismatch at exact half. The response was not repaired, the
example/gold was not changed, and no second paid call was made. This is local
demo evidence only and is not added to the nine-trace pilot or extrapolated to
overall Hy3 capability.

Credential scanning found no API key, Authorization, or Bearer value in the
interactive artifacts. The full prompt/raw/diagnosis run remains under the
Git-ignored local artifacts directory and is not committed.

## Pending

Canonical Windows/Ubuntu CMake/CTest CI is still required before the status can
be set to `interactive_greedy_diagnosis_demo_ci_verified`.
