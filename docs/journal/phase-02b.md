# 阶段日志：Phase 2B — 离线评估管线（PromptExporter / PredictionImporter / Reporter）实现与验证记录

> 日期：2026-08-24
> 状态：`phase2b_offline_pipeline_implemented_unverified_pending_planner_review`
> 分支：`phase2b-integration`（自 `main` 切出，工作区 Phase 2B-1 WIP 改动一并保留）
> 关联：Phase 2A 协议（`docs/phase-02-protocol.md`）、Prompt 模板（`prompts/hy3-evaluator-v1.md`）、指标（`docs/phase-02-metrics.md`）、路线图（`docs/roadmap.md`）、架构（`docs/architecture.md` §6.4）。

## 1. 任务授权与边界

规划方调整工作流：**取消 Phase 2B-1 单独验收，连续实现整个 Phase 2B 后统一审查**。明确边界：

- **三段式离线管线**：`export-prompts` → `import-response` / `mark-not-attempted` → `report`。
- **全程不调用模型 API、不连接 OJ、不执行候选代码**。
- **不 commit/tag/push main**；仅可推送 `phase2b-integration` 分支触发 CI；**不创建 tag、不创建 PR、不合并 main、不进入 Phase 2C**。
- 数据契约（`data/`）、Prompt 模板、协议、指标**一律冻结、不修改**。
- 测试 fixtures 属 `SYNTHETIC_TEST_FIXTURE`，不得伪装真实实验结果、不得写入正式 `experiments/` run。
- MSVC 本地编译因沙箱环境不可用，完整验证交由 GitHub CI（`phase2b-integration` 分支）。

## 2. 新增/修改文件清单

### 2.1 新增

| 文件 | 作用 |
| --- | --- |
| `include/hy3_algotrace/sha256.hpp` + `src/sha256.cpp` | FIPS 180-4 SHA-256（NIST 向量）+ UTF-8 规范化 |
| `include/hy3_algotrace/prompt_exporter.hpp` + `src/prompt_exporter.cpp` | 模板边界提取 / allowlist 投影 / structural leakage 审计 / 渲染 / run-manifest |
| `include/hy3_algotrace/prediction_importer.hpp` + `src/prediction_importer.cpp` | raw 逐字节保存+字节哈希、6 态严格判别、无 fence/repair、schema+语义校验、wrapper、显式 not_attempted |
| `include/hy3_algotrace/reporter.hpp` + `src/reporter.cpp` | gold 隔离读取、全部指标（parse/status/primary/micro/macro/pair + 混淆矩阵）、去重、零分母、N/A 区分、completed_at 控制 |
| `tests/prediction_importer_tests.cpp` | 25 项依赖自由测试 |
| `tests/reporter_tests.cpp` | 5 项依赖自由测试 |
| `tests/phase2b_e2e_tests.cpp` | 端到端 synthetic smoke（真实数据集 + 合成响应） |
| `tests/fixtures/*.json` / `*.md` / `*.txt` + `tests/fixtures/README.md` | 明确标记 `SYNTHETIC_TEST_FIXTURE` 的合成模型响应 |

### 2.2 修改

| 文件 | 修改点 |
| --- | --- |
| `src/main.cpp` | 新增 `import-response` / `mark-not-attempted` / `report` 子命令；保留 `validate` / `export-prompts` / `--help` |
| `CMakeLists.txt` | `hy3_algotrace_core` 加入 `prediction_importer.cpp` + `reporter.cpp`；新增 3 个测试可执行 + `add_test` |
| `build-msvc/build.bat` | 编译/运行新增测试与 CLI 命令 |
| `README.md` / `docs/roadmap.md` / `docs/architecture.md` | 状态、CLI、架构 §6.4、路线图 Phase 2B 同步（不再拆 2B-1/2B-2） |

## 3. 关键设计决策（三段式）

1. **SHA-256 自实现**：NIST 三向量验证；UTF-8 规范化单点（`normalizeUtf8`）。
2. **PromptExporter（WIP 沿用并自检）**：显式 allowlist 投影、structural leakage 递归 key 审计、临时目录 + 原子 rename、`prompt_template_sha256` 取 BEGIN/END 间 body、`dataset_commit` 固定 `fb40cb2…`。
3. **PredictionImporter**：
   - raw 响应**逐字节**保存，`raw_response_sha256` 按原始字节计算（不做规范化/修复）；`CRLF` 等字节差异体现在哈希中。
   - **绝不**静默剥离 Markdown 围栏/前言/尾随文本；**绝不** repair JSON；无法解析即 `invalid_json`。
   - 6 态严格判别：`model_call_not_attempted` / `empty_response` / `invalid_json` / `schema_invalid` / `semantic_invalid` / `parsed`。
   - `parse_status != parsed` 时 `prediction` 必为 JSON `null`。
   - `implementation_consistency` finding 在无关联候选时由 `classifyResponse` 拒绝（候选关联由渲染 prompt 的 `candidate_solution` 标记推断）。
   - `trace_id` 一致性：解析结果与运行上下文 `expectedTraceId` 不符 → `schema_invalid`。
   - **拒绝覆写**：已存在 raw/prediction 即 `E_RAW_EXISTS` / `E_PREDICTION_EXISTS`（v1 不实现覆盖选项）。
   - **显式 not_attempted**：`markNotAttempted` 必须由调用方显式触发；缺文件**绝不**静默推断为 not_attempted。
   - gold 隔离：wrapper 结构由协议 §7 固定，无任何 `diagnoses`；内部 `__parse_failed__` sentinel 仅在 Reporter 内存使用，**绝不**写回文件（写前防御性断言）。
4. **Reporter（指标严格按 `docs/phase-02-metrics.md`）**：
   - 唯一读取 gold 的模块；gold 只进 `report.*`，不污染 `predictions/`。
   - wrapper 缺失 → `run_complete=false`，**绝不**默认 `correct`。
   - parse 失败计分：status_accuracy 计 0；primary_category_accuracy 在分母中计 0；macro_F1 固定 0；micro/pair 用 `__parse_failed__` sentinel（内存），不套用「空对空=1」。
   - 去重：`findings[].category` 与 `(stage,category)` 对均按 trace 去重后再计 micro/macro/pair。
   - 零分母规则：`micro_precision = TP/(TP+FP)`，当 `TP+FP=0` 记 1；recall 同理；`P+R=0` 时 F1 记 0。
   - `primary_category_accuracy`：分母固定为 gold.status==incorrect 的轨迹集 `|I|`；`|I|=0` 时记 N/A（JSON null），与数值 0 区分。
   - `completed_at`：仅在 run 完整且传入合法 ISO-8601 时写回 `run-manifest.json`。
   - 确定性：时间字段全部由参数注入（`completed_at` / `generated_at`），不隐式取当前时间；report.json 2 空格缩进稳定。
5. **CLI 退出码**：参数错误 2；输入/schema/语义/run 不完整等业务失败 1；成功 0。

## 4. 测试分类与计数

| 测试可执行 | 项数 | 重点 |
| --- | --- | --- |
| `validator_tests` | 56 | 数据契约回归（不变） |
| `prompt_exporter_tests` | 22 | SHA 向量、CRLF/BOM、UTF-8/NUL、模板边界、占位符、allowlist、leakage 非误报、null 候选、cf_160A_t3 关联、9 轨迹字典序、确定性、拒绝覆写、unsafe-id、真实数据集成 |
| `prediction_importer_tests` | 25 | 空/空白、非 JSON、Markdown fenced、尾随文本、缺键/错类型/额外键/非法枚举、confidence 非 null、correct/incorrect/undetermined 语义、primary 不在 findings、implementation_consistency 无候选拒绝、trace_id 不一致、raw 字节哈希、拒绝覆写、not_attempted、确定性、无 sentinel |
| `reporter_tests` | 5 | 完美预测、部分解析失败、缺 wrapper→incomplete、同 category 去重、completed_at 仅在完整时更新、report.json/md 一致 |
| `phase2b_e2e_tests` | 1 | 端到端 synthetic smoke：9 prompt 导出 → 导入合成响应 → 显式 mark 其余 → 报告；无 gold 泄漏、report 数值确定、completed_at 更新 |

> 合计 109 项（含 56 项回归）。所有 fixture 标记 `SYNTHETIC_TEST_FIXTURE`。

## 5. 自检查（提交前，非编译类）

| 检查项 | 结果 |
| --- | --- |
| `git diff --check` | 通过（无 trailing-whitespace 错误） |
| `git diff -- data` | 空（数据契约未改） |
| 新增 `.py` 文件 | 无 |
| 冻结文件（`data/` / 模板 / 协议 / 指标）修改 | 无 |
| 未调用模型/API/OJ/候选代码 | 是（全离线，仅 CLI 文件操作） |
| 未生成伪造实验结果 | 是（fixtures 明确标记 synthetic，未写 `experiments/`） |
| 未推 main / 未 tag / 未进入 Phase 2C | 是（仅 `phase2b-integration` 分支） |

## 6. 验证状态

| 验证项 | 结果 |
| --- | --- |
| 本地 MSVC `/W4`（`build-msvc/build.bat`） | **本会话沙箱未能执行**：Windows SDK(ucrt) 缺失，`cmd.exe` 与 WSL 均被安全策略禁用 |
| GitHub CI（Windows+Linux CMake/CTest） | 待 `phase2b-integration` 分支推送后 `gh workflow run ci.yml --ref phase2b-integration` 执行（最多 3 次修复循环） |

## 7. 待统一规划方审查 / 后续步骤

1. CI 全绿后，规划方做一次 Phase 2B 统一审查（不拆分 2B-1/2B-2）。
2. 审查通过后进入 **Phase 2C**：用冻结模板与 `reference_assisted` 模式对 9 条轨迹做离线冒烟实验（人工/脚本交给 Hy3，取回原始响应，跑 `import-response` + `report`）。

## 8. 风险与未决项

- **编译验证缺口**：本地无 SDK，无法跑 MSVC；代码已做 C++17 标准写法人工静态审查，但编译期零警告/零错误最终由 GitHub CI 确认。
- **语义泄漏**：structural（key）审计 + 排除 `test_cases.notes`；自由文本语义泄漏需人工/后续规则审计（协议 §3.4）。
- **不宣称代表总体能力**：本阶段仅交付可复现工具与 synthetic 验证，不产出任何真实评测结论。
