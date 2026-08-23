# Phase 0 日志 — 设计选择与探索记录

> 本文件是 hy3-algotrace 的实验探索历程起点，记录本阶段的决策与未决问题。
> 关联文档：架构见 `architecture.md`；数据契约见 `data-contract.md`；路线图见 `roadmap.md`。

## 1. 设计选择

- **技术栈选 C++17 而非 Python**：项目归属「混元大语言模型项目」算法竞赛解法评估，预期需要可控的构建与评测管线；C++17 与参赛者解法语言一致，便于后续直接比对 / 验证。
- **先冻结范围再动手**：Phase 0 只建骨架与文档约定，不实现评测算法，避免过早耦合。
- **仅贪心题起步**：推理链清晰、错误边界明确、数据可得，适合先跑通「过程评估」方法论。
- **三职责解耦**：算法类型识别 / 过程评估 / 代码验证 拆分，便于独立演进与未来自动路由。
- **数据契约语言无关**：用结构化字段（类 JSON）约定，便于未来 C++17 读取；本阶段不写解析器。
- **目录克制**：include / src / data / experiments / tests 仅为预留空目录，未塞占位代码（仅保留 `.gitkeep` 与最小 `main.cpp`）。

## 2. 假设

- CodeContests Verified 中 Codeforces 来源题目足以支撑 ~12 道贪心题的初版样本。
- 六环节拆解（题意理解 → 贪心策略 → 贪心证明 → 复杂度 → 边界 → 实现一致性）可覆盖贪心题主要推理失败点。
- 人工标注在 Phase 1–3 可行，Phase 6 再引入自动路由。

## 3. 规划方决定（原未决项已决议）

以下原未决问题已由规划方在 Phase 0 内裁定，记录于此以便后续阶段引用：

- **Phase 3 基线规模**：接受约 12 题 × 每题约 6 条轨迹 = 约 72 样本；这是**试验基线，不是硬性上限**。
- **样本构造方式**：允许基于公开题面由人工编写推理轨迹，但必须保存**来源、构造方式与标注信息**；不得把人工改写内容伪装成官方内容。
- **Phase 2 测试框架**：暂不引入 Catch2 / GTest 等外部依赖，优先使用 **CTest + 简单 C++ 测试可执行文件**。
- **Phase 4 准确率阈值**：在获得 Phase 3 基线结果后再确定；当前仅预留指标定义，不预设数值。
- **confidence 标定**：Phase 4 之前保持 **null**，不产生未经校准的数值；示例不得填写随意数值。

## 4. 实际创建过程

- 于 `summer-of-code/hy3-algotrace/` 下创建目录骨架（docs/journal、include、src、data/problems、experiments、tests）。
- 创建文档：README.md、docs/architecture.md、docs/data-contract.md、docs/error-taxonomy.md、docs/roadmap.md、docs/journal/phase-00.md。
- 创建最小 C++17 骨架：CMakeLists.txt、src/main.cpp（仅占位，无业务逻辑）、预留空目录（.gitkeep 占位）。
- 全程未修改 `hy3-algotrace/` 之外任何文件，未复用旧项目代码，未创建 Python 文件。
- 自检验证：文件均在目标目录内、术语与阶段编号一致、未越界实现后续功能。

### 4.1 数据契约修订（Phase 0 内）

经审查，对 `docs/data-contract.md` 做了如下修正（仍属 Phase 0，未实现解析器 / 评测代码）：

- 示例题目改用正确的 Codeforces 编号 **1398B（Substring Removal Game）**，并同步改正 problem id / trace id 及所有引用字段；重写了轨迹与诊断，使其策略、错误类别与 evidence 逻辑一致（见 data-contract 第 7 节）。
- 增设 `test_case`（测试信息）与 `verification_result`（运行验证结果）结构。
- 诊断顶层由单一 `overall` 改为 `status`（correct / incorrect / undetermined）+ `primary_category` + `findings`（允许多错误），并明确 `primary_category` 选取规则。
- `insufficient_evidence` 不再作为已确认错误类别，改为映射到 `status = undetermined`；与 `missing_greedy_proof` 的区别已写入 error-taxonomy。
- `confidence` 改为可选 / 可空，新增 `confidence_method` 与 `calibration_version`；示例置为 null，未校准不得用于正式比较。
- 增设可复现元数据：`schema_version`、`taxonomy_version`、`source_reference`、`trace_origin`、`annotator`、`dataset_version`、`created_at`。
- 同步更新 README、architecture、error-taxonomy 与本文档，术语保持一致。
- 数据契约版本：`schema_version = 0.2.0`，`taxonomy_version = 1.0.0`。

### 4.2 数据契约第二轮修订（Phase 0 内，仅文档）

Phase 0 第二轮验收发现少量事实性与内部一致性问题，再次限定修订（仍只改文档，未进入 Phase 1，未实现解析器 / 评测代码）：

- **错误类别计数校正**：`correct`、`incorrect`、`undetermined` 是 `diagnosis.status`，不是 finding 类别。finding 类别实为 7 类（problem_misunderstanding、wrong_greedy_choice、missing_greedy_proof、invalid_greedy_proof、complexity_error、boundary_omission、implementation_mismatch）。将各文档中错误类别计数由 8 改为 7（primary_category 与 finding 说明的「X 类之一」表述修正为「7 类之一」）。
- **CF 1398B 题意修正**：题面须准确为「每回合可以删除一个由相同字符组成的非空连续子串（连续 0 或连续 1 均可）；得分是自己删除的字符 1 的个数；双方最优，求先手最大得分」，不再写成「只能删除连续 1 段」。
- **测试信息语义明确**：`test_case.input` 为可直接传给候选程序的完整标准输入，`expected_output` 为对应完整标准输出；示例改为 `input: "1\n011101\n"` / `expected_output: "3\n"` 与 `input: "1\n0000\n"` / `expected_output: "0\n"`，两组均标记 `manually_designed`（非官方样例，不伪装官方）。
- **示例轨迹重写以自洽**：错误策略保持「按 1 段出现顺序取，不按长度排序」；`problem_understanding` 含允许删 0；`complexity` 改为该错误策略的 O(n)（非排序 O(n log n)）；`boundary` 显式写出「字符串非空所以不需处理特殊情况」从而确实遗漏全 0；删除 `cpp_solution` 字段与 `implementation_consistency` 步骤，不声称代码一致。
- **诊断 findings** 至少含 `wrong_greedy_choice`（根因，作为 primary_category）与 `boundary_omission`；并补充 `invalid_greedy_proof`（「靠前段优先出现」论证不成立），三者均保留。
- **参考证明改进**：明确核心支配关系——得分只来自删除 1；最优下删 0 或只删某 1 段部分不优于取得完整段；故归约为双方轮流选剩余 1 段长度；每回合取最长段等价于段长降序排序后先手取第 1、3、5…… 项。
- **自检**：以 PowerShell `ConvertFrom-Json` 验证示例 JSON 合法；确认错误类别计数已统一为 7 类、题意已改为允许删除连续 0 或连续 1、manually_designed 用例未标 official_sample、错误策略 / 复杂度 / findings 一致；未改动任何 C++ 文件。
