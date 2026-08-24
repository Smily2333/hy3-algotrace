# Phase 2C journal

## Production transport and audit gate (2026-08-24)

- Implementation commit: `a0a024e`；Ubuntu dependency fix: `e16a3e6`。
- Canonical CI: <https://github.com/Smily2333/hy3-algotrace/actions/runs/32734561463>。
- Windows and Ubuntu both passed Configure, Build, all 9 CTest executables, and CLI validation.
- Windows transport uses system WinHTTP. Ubuntu CI used the signed Ubuntu package
  `libcurl4-openssl-dev 8.5.0-2ubuntu10.12` (`libcurl 8.5.0`, curl license).
- `model-calls/<trace_id>.json` is written before send and atomically finalized;
  any existing sidecar permanently refuses another call for that trace.

## Single authorized project canary

- Run: `phase2c-canary-cf-160a-t3-20260824`; trace: `cf_160A_t3`.
- Exactly one non-retrying POST to the pinned official TokenHub origin.
- Result: HTTP `200`; request ID `9d7283e4-95d1-4db1-bff9-c353b1e33ccf`;
  latency `3097 ms`; model `hy3`; model version unavailable (`null`).
- Usage: prompt `2715`, completion `228`, total `2943` tokens.
- Raw model-content SHA-256:
  `194253d2cfbb263ea406e61a476986bb54931577c55f27d9c694b554444015d8`.
- Strict Importer result: `parsed`; prediction: `incorrect` /
  `implementation_mismatch`.
- The local ignored run contains one sidecar, one raw response, and one prediction.
  The remaining eight traces were not called. No aggregate metric was computed.
- Exact credential scan found no API key in tracked files or local canary artifacts.
  No candidate code was executed and no OJ was contacted.
