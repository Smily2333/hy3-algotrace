# 阶段路线图

> 宏观阶段规划。当前处于 **Phase 1B**，R2.1 已通过规划方技术验收，等待 GitHub CI。
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

### Phase 1B — C++17 数据契约校验器（规划方验收通过，等待 GitHub CI）

- **目标**：实现纯 C++17 命令行工具 `hy3_algotrace validate`，加载并校验数据集对照 0.3.0 契约。
- **主要产物**：`include/hy3_algotrace/*`、`src/*`、`tests/validator_tests.cpp`（56 项测试全过）、`third_party/nlohmann/json.hpp`（v3.12.0，SHA-256 校验）、`CMakeLists.txt`（canonical，含 `hy3_algotrace_core` 静态库 + SYSTEM PUBLIC third_party）、`docs/journal/phase-01b.md`。
- **状态**：`phase1b_planner_reviewed_pending_github_ci`（codex_planner 技术验收已通过，等待 GitHub CI 补齐 canonical CMake/CTest 与跨平台验证）。CMake/CTest 未实际运行（`cmake_ctest_status = unverified_tool_unavailable`）；跨平台未验证（`cross_platform_status = unverified`）；未提交 / 未推送 / 未进入 Phase 2。
- **进入下一阶段条件（Phase 2）**：规划方复核通过并推送 GitHub（目标分支 `main`）；在具备 CMake 的环境 / CI 中补做 canonical CMake/CTest 验证。

## Phase 2 — C++17 基础评测管线

- **目标**：搭通 Ingest → ProcessEvaluator → Reporter 的最小管线。
- **主要产物**：C++17 模块骨架（不含自动路由）；规则化过程评估初版；报告输出。
- **进入下一阶段条件**：管线可对样本产出结构化诊断报告；与 data-contract 对齐。

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
