# Phase 2C journal

## Production transport and audit gate (2026-08-24)

- Implementation commit: `a0a024e`；Ubuntu dependency fix: `e16a3e6`。
- Canonical CI: <https://github.com/Smily2333/hy3-algotrace/actions/runs/32734561463>。
- Windows and Ubuntu both passed Configure, Build, all 9 CTest executables, and CLI validation.
- Windows transport uses system WinHTTP. Ubuntu CI used the signed Ubuntu package
  `libcurl4-openssl-dev 8.5.0-2ubuntu10.12` (`libcurl 8.5.0`, curl license).
- `model-calls/<trace_id>.json` is written before send and atomically finalized;
  any existing sidecar permanently refuses another call for that trace.

## Frozen nine-trace pilot summary

- Evaluation protocol: `0.1.0`; input mode: `reference_assisted`.
- Dataset: `phase1a-pilot-001` at
  `fb40cb2f8f93967a93a376508c5a0d9c3f3f4df9`.
- Prompt: `hy3-evaluator-v1`, SHA-256
  `a62bdb59dfb7af1cdb1be0eeb4192c68fe48520b6ef3e8d2241194c97bcadab7`.
- Model: `hy3`; model version unavailable (`null`).
- Run interval: `2026-08-24`, completed at `2026-08-24T14:07:39Z`.
- Trace IDs: `cf_1398B_t1`, `cf_1398B_t2`, `cf_1398B_t3`,
  `cf_160A_t1`, `cf_160A_t2`, `cf_160A_t3`, `cf_545D_t1`,
  `cf_545D_t2`, `cf_545D_t3`.
- Each trace had exactly one non-retrying POST. HTTP success and strict
  `parse_status=parsed` were both `9/9`.
- Token usage: prompt `24,395`; completion `2,066`; total `26,461`.
- Frozen metrics: status accuracy `1.0000`; primary-category accuracy `1.0000`;
  finding micro precision / recall / F1 `0.6154 / 1.0000 / 0.7619`;
  finding macro F1 `0.8778`; stage-category-pair micro F1 `0.7619`;
  undetermined rate `0.0000`.
- Confidence, confidence method, and calibration version remained `null`.

Raw model-content SHA-256 audit list (raw bodies are not committed):

| trace_id | raw_response_sha256 |
| --- | --- |
| `cf_1398B_t1` | `2be77d66a6a3a5ddf2561af5177abe20b819d2c67f2f355c600bf7215ab73cff` |
| `cf_1398B_t2` | `2c6f0b835b1810f3beae8140f7e642a148345e06188b19fc71a99075a423a363` |
| `cf_1398B_t3` | `e22adbeba81fb1be35d6a80ea77629918dc4b9af529462ca6d391578e17f7471` |
| `cf_160A_t1` | `43ae252958c37e623d73608b089b7c4a355c613194342bbba6db0e07b1e6b0b8` |
| `cf_160A_t2` | `aa3668a64cb6259be419fbcc2016f705c55b7c44f8e9386815d164d1ef82f81f` |
| `cf_160A_t3` | `194253d2cfbb263ea406e61a476986bb54931577c55f27d9c694b554444015d8` |
| `cf_545D_t1` | `b4c297c06b65bddabe4e19a9c957833db58700f787612a385ced230508334817` |
| `cf_545D_t2` | `7d148e9c9698b3fa3be0ba906a6b93b7a24d4ec827daec332cbbe5bb4a6d3766` |
| `cf_545D_t3` | `88b99254d08370dae35e3fe8473af50e0e8b3bce4e9cc0d684ca64b068ca26a3` |

### Finding-scope limitation

The model produced five findings outside the frozen gold finding sets:
`cf_160A_t2` added `wrong_greedy_choice`, `invalid_greedy_proof`, and
`boundary_omission`; `cf_545D_t2` added `invalid_greedy_proof` and
`boundary_omission`. They remain false positives under the frozen metrics.

Two interpretations must remain visible: the model may be over-diagnosing
downstream consequences of one root cause, while the current gold findings may
also be non-exhaustive for reasonable secondary errors. Neither gold nor metrics
were changed after the run. Before dataset expansion, multi-label annotation
rules must explicitly define root-cause versus downstream findings.

This pilot contains only nine frozen traces and must not be generalized to Hy3
overall capability. No raw response, credential, authorization header, local
path, or complete run directory is committed. No candidate code was executed
and no OJ was contacted during Phase 2C.
