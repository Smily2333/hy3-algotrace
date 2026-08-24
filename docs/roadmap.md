# 阶段路线图

> 宏观阶段规划。Phase 2B 已在 commit `385c48e` 完成 Windows/Ubuntu CI 技术验收；Phase 2C production transport 与 `model-calls` 侧车已通过 Windows/Ubuntu CI run `32734561463`。唯一一次 `cf_160A_t3` 正式 canary 已完成，当前状态 `phase2c_single_canary_completed_pending_remaining_pilot_authorization`。Prompt、指标和数据继续冻结；其余 8 条未调用，9 条真实 Hy3 pilot 尚未完成。
> 关联文档：架构见 `architecture.md`；数据契约见 `data-contract.md`；错误分类见 `error-taxonomy.md`。

## Phase 0 — 范围与骨架

- **目标**：冻结范围，建立项目骨架与文档约定。
- **主要产物**：目录骨架、README、architecture、data-contract、error-taxonomy、roadmap、journal/phase-00；最小 C++17 占位。
- **进入下一阶段条件**：范围获规划方确认；文档术语 / 阶段编号一致；未越界实现后续功能。

## Phase 1 — 数据契约及少量人工样本

> Phase 1 拆分为 **1A（数据）** 与 **1B（C++17 校验器）** 两个落地子阶段。

### Phase 1A — 数据格式冻结与首批样本（已完成，2026-08-23）

- **目标**：冻结数据契约 0.3.0，产出并复核首批模型生成样本。
- **主要产物**：`data/manifest.json` + 3 题 × 3 轨迹 = 9 条样本；通过 Codex 规划方（codex_planner）技术复核（review_status=planner_reviewed）；journal/phase-01a。
- **状态**：已完成。

### Phase 1B — C++17 数据契约校验器（已完成，CI 已验证）

- **目标**：实现纯 C++17 命令行工具 `hy3_algotrace validate`，加载并校验数据集对照 0.3.0 契约。
- **主要产物**：`include/hy3_algotrace/*`、`src/*`、`tests/validator_tests.cpp`（56 项测试全过）、`third_party/nlohmann/json.hpp`（v3.12.0，SHA-256 校验）、`CMakeLists.txt`（canonical，含 `hy3_algotrace_core` 静态库 + SYSTEM PUBLIC third_party）、`docs/journal/phase-01b.md`。
- **状态**：`phase1b_complete_ci_verified`。codex_planner 技术验收已通过（**不等同**人工 human_reviewed 或专家 expert-reviewed 审查）；实现 commit `8145b4f1b894101a8cbb1a302c6028b4fe8b3a01` 已推送 GitHub main；GitHub CI run 32656643095 在 `windows-latest` 与 `ubuntu-latest` 上 Configure / Build / CTest / Run CLI 全部 success（`cmake_ctest_status = verified_github_ci`，`cross_platform_status = verified_windows_linux`；macOS 未验证）。本地 MSVC 56/56 保持通过。
- **进入下一阶段条件（Phase 2）**：
  - 规划方技术验收：已完成（codex_planner，2026-08-24）。
  - main 推送：已完成（commit `8145b4f…`）。
  - canonical CMake/CTest：已完成（GitHub CI 验证）。
  - Windows/Linux CI：已完成（run 32656643095，双平台 success）。
  - Phase 2 尚未开始，等待规划方对 Phase 1B 的最终确认与 Phase 2 启动授权。

## Phase 2 — 离线评估协议与基础评测管线（拆分为 2A–2D）

### Phase 2A — 离线评估协议与 Prompt 模板（已完成）

- **目标**：冻结公平、可复现、无标签泄漏的 Hy3 离线评估协议：研究问题、`reference_assisted` 输入模式、allowlist/denylist、单条处理流程、失败状态、实验运行目录、`prediction` wrapper、输出契约、评价指标与可复用 Prompt 模板。
- **主要产物**：`docs/phase-02-protocol.md`、`docs/phase-02-metrics.md`、`prompts/hy3-evaluator-v1.md`、`docs/journal/phase-02a.md`；同步 `README.md` / `roadmap.md` / `architecture.md`。
- **状态**：`phase2a_complete_planner_reviewed`（codex_planner 技术验收通过，2026-08-24；不等同 human/expert review）。该阶段未运行 9 轨迹 Hy3 实验；后续 Phase 2B 已另行完成。
- **进入下一阶段条件**：规划方复审通过 Phase 2A 协议与 Prompt 模板。

### Phase 2B — 离线评估管线：PromptExporter / PredictionImporter / Reporter（已完成）

- **目标**：实现完整 C++17 离线评估管线三段式：`export-prompts`（确定性导出无泄漏 Prompt）、`import-response` + `mark-not-attempted`（逐字节保存 raw、6 态严格判别、schema/语义校验、生成 prediction wrapper、gold 隔离）、`report`（严格按 `docs/phase-02-metrics.md` 汇总指标、report.json/md 一致、completed_at 仅完整时更新）。**全程不调用模型 API、不连接 OJ、不执行候选代码。**
- **主要产物**：
  - `include/hy3_algotrace/sha256.hpp` + `src/sha256.cpp`（FIPS 180-4 SHA-256 + UTF-8 规范化）
  - `include/hy3_algotrace/prompt_exporter.hpp` + `src/prompt_exporter.cpp`（模板边界提取 / allowlist 投影 / structural leakage audit / 渲染 / run-manifest）
  - `include/hy3_algotrace/prediction_importer.hpp` + `src/prediction_importer.cpp`（raw 逐字节保存 + 字节哈希、6 态判别、无 fence/repair、schema+语义校验、wrapper、显式 not_attempted）
  - `include/hy3_algotrace/reporter.hpp` + `src/reporter.cpp`（gold 隔离、全部指标、去重、零分母、N/A 区分、completed_at 控制）
  - CLI 四命令（`export-prompts` / `import-response` / `mark-not-attempted` / `report`）+ `validate` / `--help`
  - 测试：`prompt_exporter_tests`(22) + `prediction_importer_tests`(25) + `reporter_tests`(5) + `phase2b_e2e_tests`(synthetic smoke) + `validator_tests`(56 回归)；`CMakeLists.txt` 全部接入 CTest；`tests/fixtures/`（标记 `SYNTHETIC_TEST_FIXTURE`，绝不伪装真实实验）
  - `docs/journal/phase-02b.md` 统一记录
- **状态**：`phase2b_complete_ci_verified_pending_planner_release_decision`（commit `385c48e`；Windows/Ubuntu CI run `32712043144` 全绿）。该状态不等同 human/expert review；未创建 tag/Release。
- **进入下一阶段条件（Phase 2C）**：规划方统一复审通过 Phase 2B 实现与测试；CI 在 Windows+Linux 全绿；确认无 gold 泄漏、no model/API/OJ/candidate 调用、报告数值一致；然后启动 9 条轨迹离线冒烟实验。

### Phase 2C — Hy3 9 轨迹冒烟实验

- **目标**：用冻结的 `hy3-evaluator-v1` 模板与 `reference_assisted` 模式，对 Phase 1A 的 9 条贪心轨迹做离线推理（人工/脚本交给 Hy3），运行 Reporter，记录指标。
- **主要产物**：`experiments/phase-02/runs/<run_id>/` 完整产物（prompts / raw-responses / predictions / report）；冒烟级指标（见 `docs/phase-02-metrics.md` 第 12 节规模限制）。
- **当前进度**：已实现 `IModelClient` / `ModelRunner` / `FakeModelClient`、官方 TokenHub adapter、Windows WinHTTP / Linux libcurl production transport、逐次调用审计 sidecar 与 synthetic 垂直 smoke；Windows/Ubuntu CI 已通过，唯一一次 `cf_160A_t3` canary 已完成并严格解析。其余 8 条未获授权，不计算整体指标。
- **进入下一阶段条件**：9 条样本全部产生 `parsed` 或明确失败状态；指标可复现；不宣称代表总体能力。

### Phase 2D — CandidateRunner 与代码验证扩展

- **目标**：引入本地受限的候选代码编译与运行（`CandidateRunner` / `CodeVerifier`），为 `implementation_consistency` 提供实证信号。Phase 2D 初版仅进行**本地受限**的 C++ 编译与运行，包含超时控制、stdin/stdout 对比与 `verification_result` 生成；**不连接、不提交外部 OJ**；OJ 对接只能作为未来可选扩展，必须另行授权。
- **主要产物**：`CandidateRunner`、`CodeVerifier` 增强；与 `code_test_verification` 职责线对齐。
- **进入下一阶段条件**：候选解法可被编译/运行并产出 `verification_result`；`implementation_consistency` 环节具备实证依据。

## Phase 3 — 贪心题小规模实验

- **目标**：用 ~12 题 / ~72 样本跑通实验并记录。
- **主要产物**：完整 72 样本集；experiments 记录；基线结果。
- **进入下一阶段条件**：样本齐备且可复现；结果可追溯。

## Phase 4 — 错误定位与评分校准

- **目标**：提升定位精度，校准评分 / 置信度。
- **主要产物**：评分规则；置信度标定；定位准确率评估。
- **进入下一阶段条件**：定位与人工标注一致率达到预定义阈值（阈值待规划方定）。

## Phase 5 — 扩大数据规模

- **目标**：扩充贪心题样本量与多样性。
- **主要产物**：更大样本集；难度 / 类型分层。
- **进入下一阶段条件**：规模与多样性达标，管线稳定。

## Phase 6 — 自动算法类型路由

- **目标**：用 `algorithm_type_identification` 自动识别算法类型，替代人工标注透传。
- **主要产物**：TypeRouter 自动实现；类型分布统计。
- **进入下一阶段条件**：路由准确率达阈值；greedy 之外至少可区分 search / dp / graph 雏形。

## Phase 7 — 扩展到搜索、动态规划等类型

- **目标**：将过程评估与验证扩展到其它算法类型。
- **主要产物**：各类型错误子分类；对应评估规则；跨类型报告。
- **进入下一阶段条件**：每类有可用样本与评估规则；与 greedy 共用框架。

## Phase 8 — 总结、复现实验与最终交付

- **目标**：汇总方法、复现全部实验、产出最终交付物。
- **主要产物**：总结文档；复现脚本；最终评测器与示例；验收材料。
- **进入下一阶段条件**：可一键复现；文档与代码一致；通过验收。
