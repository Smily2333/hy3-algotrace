# Interactive greedy diagnosis demo

This local demo exposes the already validated Hy3 diagnosis capability as a
small Chinese web application. It is separate from the frozen evaluation
pipeline: it does not read gold diagnoses, reference verdicts, or the dataset
manifest, and its results never enter Phase 2 metrics.

## Start and scope

From the repository root, build the canonical CMake target and run:

```text
hy3_algotrace_demo --host 127.0.0.1 --port 8080
```

Then open `http://127.0.0.1:8080/`. The server accepts loopback hosts only.
`TOKENHUB_API_KEY` is read by the existing C++ Hy3 client from the server
process environment; it is never included in HTML, JavaScript, API responses,
or audit files.

The demo supports only user-supplied greedy reasoning. Optional C++17 source is
sent to Hy3 for static semantic review only. It is never compiled or executed,
no online judge is contacted, and the result is not a formal proof.

## Request contract

`POST /api/diagnose` requires `Content-Type: application/json` and this shape:

```json
{
  "request_id": "safe unique identifier",
  "algorithm_type": "greedy",
  "problem": {
    "title": "required",
    "statement": "required",
    "input_format": "required",
    "output_format": "required",
    "constraints": "required"
  },
  "reasoning": "required",
  "cpp_solution": null,
  "test_cases": [
    {"input": "optional", "expected_output": "optional", "note": "optional"}
  ]
}
```

The server strictly checks JSON types, required fields, identifiers, field
lengths, test count, and the fixed `greedy` algorithm type before any model
call. The HTTP body is capped independently. A duplicate request identifier is
latched before invocation and cannot produce a second POST.

Successful responses contain `diagnosis` and safe `metadata`. Diagnosis status
is `correct`, `incorrect`, or `undetermined`; findings use the frozen seven
error categories and six reasoning stages. Metadata contains model identity,
duration, safe provider request ID when available, token counts when available,
and prompt/response SHA-256 values. It never contains the model raw response,
HTTP headers, credentials, or server filesystem paths.

Transport/configuration failures use a separate `error.kind` such as
`authentication_error`, `rate_limited`, `timeout`, or `transport_error` and do
not masquerade as a diagnosis. Invalid model JSON is not repaired and Markdown
fences are not stripped.

## Prompt and local audit

The independent template is `hy3-interactive-diagnosis-v1`:

- file: `prompts/hy3-interactive-diagnosis-v1.md`
- SHA-256: `f1a0b0e5dfdc854efed55becadc9d7b45051794786ad87f0e77c74297a1b1c3a`
- model: `hy3`; model version remains `null` unless returned by the provider

The template contains no gold label, reference verdict, expected diagnosis, or
dataset metadata. It treats test cases as static context and explicitly forbids
claims that code was executed.

Each accepted request writes its one-shot audit under
`experiments/interactive/runs/<request_id>/`. The Git-ignored directory may
contain the rendered prompt, verbatim raw model response, strict diagnosis, and
a credential-free model-call sidecar. User input and raw responses must not be
committed.

## HTTP dependency and security boundary

The loopback server uses vendored
[`cpp-httplib`](https://github.com/yhirose/cpp-httplib) `v0.51.0` (`d66d9a9`),
licensed under MIT. The exact header/license hashes are recorded beside the
vendored files. Canonical builds perform no dependency download.

The server has a bounded request body, sends restrictive browser headers, does
not enable cross-origin access, performs no automatic retry, and defaults to
`127.0.0.1`. This is a local product demo, not an authenticated multi-user
service and not a code sandbox. Do not expose it through port forwarding or a
public reverse proxy.
