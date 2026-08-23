# Phase 1A 日志 — 数据格式冻结与首批人工审查样本

> 本文件记录 Phase 1A 的设计选择、覆盖矩阵与未决项。
> 关联文档：数据契约见 `data-contract.md`（schema 0.3.0）；错误分类见 `error-taxonomy.md`；路线图见 `roadmap.md`。
> 范围约束：本阶段**只**完成数据格式冻结与首批样本；不实现 C++ 解析器，不进入 Phase 1B / Phase 2。

## 1. 三道题的选择理由

| 题号 | 名称 | 选择理由 |
| --- | --- | --- |
| CF 160A | Twins | 极简贪心（降序取币），但包含微妙的『严格大于 vs 大于等于』语义陷阱，便于构造 `problem_misunderstanding` 与 `implementation_mismatch`（思路对、代码用错比较符）。单测试样例、约束小，适合作为首题。 |
| CF 545D | Queue | 经典『重排使最多人不失望』贪心，正确策略是升序排序。易于构造 `wrong_greedy_choice`（保持原顺序）与 `complexity_error`（忽略排序声称 O(n)），且反例清晰。 |
| CF 1398B | Substring Removal Game | 两人博弈 + 段长排序的贪心，边界（全 0 串）与证明要求突出，便于构造 `missing_greedy_proof`、`invalid_greedy_proof` 与 `boundary_omission`（多错误轨迹）。注意该题是 **T 组测试**，官方样例为多测试输入。 |

三题均来自官方来源，且均为真实 Codeforces 贪心题，覆盖不同陷阱类型，能在本批次内让 7 类错误全部出现至少一次。

## 2. 九条轨迹的覆盖矩阵

每题恰好 3 条轨迹，共 9 条。状态与主要错误类别如下：

| 题 | trace id | 轨迹定位 | status | primary_category | 其他 findings |
| --- | --- | --- | --- | --- | --- |
| 160A | cf_160A_t1 | 完整正确 | correct | null | （无） |
| 160A | cf_160A_t2 | 题意误解 | incorrect | problem_misunderstanding | — |
| 160A | cf_160A_t3 | 思路对/代码错 | incorrect | implementation_mismatch | —（附带未运行 candidate_solution） |
| 545D | cf_545D_t1 | 完整正确 | correct | null | （无） |
| 545D | cf_545D_t2 | 错误贪心策略 | incorrect | wrong_greedy_choice | — |
| 545D | cf_545D_t3 | 复杂度分析错 | incorrect | complexity_error | — |
| 1398B | cf_1398B_t1 | 完整正确 | correct | null | （无） |
| 1398B | cf_1398B_t2 | 缺失贪心证明 | incorrect | missing_greedy_proof | — |
| 1398B | cf_1398B_t3 | 多错误轨迹 | incorrect | wrong_greedy_choice | invalid_greedy_proof、boundary_omission |

### 7 类错误覆盖情况

| 错误类别 | 出现次数 | 所在 trace |
| --- | --- | --- |
| problem_misunderstanding | 1 | cf_160A_t2 |
| wrong_greedy_choice | 2 | cf_545D_t2、cf_1398B_t3 |
| missing_greedy_proof | 1 | cf_1398B_t2 |
| invalid_greedy_proof | 1 | cf_1398B_t3 |
| complexity_error | 1 | cf_545D_t3 |
| boundary_omission | 1 | cf_1398B_t3 |
| implementation_mismatch | 1 | cf_160A_t3 |

> 全部 7 类错误均在首批样本中至少出现一次；正确轨迹 findings 为空。

## 3. 样本生成者与审查状态

- **样本生成者**：全部 9 条推理轨迹由 **Hy3（模型生成）** 产出，文件路径与字段中均标记 `trace_origin = model_generated`、`generator_model = hy3`、`annotator = hy3_draft`。
- **不得伪装**：未将任何轨迹标记为 `human_written` 或 `expert-reviewed`；未将模型生成内容冒充当人工或官方内容。
- **当前审查状态**：所有样本 `review_status = pending_planner_review`、`reviewer = null`、`reviewed_at = null`，**等待规划方复核**。
- 本批次 `confidence` 全部为 `null`（Phase 4 校准前不得填写）。

## 4. 测试用例来源分布

每题 3 组测试（normal / boundary / counterexample），共 9 组：

| 来源 origin | 数量 | 说明 |
| --- | --- | --- |
| official_sample | 3 | 每题 1 组，逐字复制自官方题面并记录官方 URL（160A_c1、545D_c1、1398B_c1） |
| manually_designed | 3 | 每题 1 组边界（n=1 或全 0 串），基于公开题面自行构造，未标官方 |
| counterexample | 3 | 每题 1 组，专门击破某条错误轨迹，notes 注明击破对象与原因 |

反例对照：
- `cf_160A_c3`（5 5）→ 击破 `cf_160A_t2`（>= 误读）与 `cf_160A_t3`（代码 >=）。
- `cf_545D_c3`（5 1）→ 击破 `cf_545D_t2`（保持原顺序）。
- `cf_1398B_c3`（1011111010101）→ 击破 `cf_1398B_t3`（按出现顺序取，正确应为 7 而非 3）。

## 5. 未执行 / 未使用的明确声明

- **未执行候选代码**：本阶段不运行任何 C++ 解法。唯一附带解法 `cf_160A_sol1` 的 `execution_status = not_run`；所有问题文件 `verification_results = []`。
- **未填写虚构结果**：所有 `verification_result.actual_output` / `runtime_ms` 未出现（数组为空）；`candidate_solution` 未声称已编译或运行。
- **未使用 Python**：全程仅用 JSON 与 Markdown 撰写；自检仅用 Node `JSON.parse`（规范允许），未编写任何 `.py` 文件。
- **未实现 C++ 解析器**：未修改 `CMakeLists.txt`、`src/`、`include/` 或任何 C++ 工程文件。
- **未连接 OJ 提交**：未向 Codeforces 提交任何代码，未批量下载数据集，未爬取其他题目，未复制无来源第三方题解。

## 6. 遇到的不确定事实

- **1398B 为 T 组测试**：官方样例本身就是 5 组输入；`test_case.input` 按契约要求是『可直接传给候选程序的完整标准输入』，故该测试直接承载完整多测试输入，而非拆成单组。
- **160A / 545D 为单测试**：输入即 n + 一行数值，无需 T 前缀。
- **反例数值选择**：160A 反例采用自构的 `2\n5 5\n`（而非直接复用官方 `2\n3 3\n`），以明确 origin=counterexample 而非 official_sample，避免来源混淆；其区分效果与官方样例一致。
- 以上均不影响数据契约冻结，列为待规划方审查时确认的事项。

## 7. 数据契约变更摘要（相对 Phase 0 的 0.2.0）

- `schema_version` 0.2.0 → **0.3.0**；`taxonomy_version` 维持 **1.0.0**。
- 明确**一题一文件**批量结构：`data/problems/<id>.json` 顶层含 `meta / problem / reference_verdict / test_cases[] / reasoning_traces[] / candidate_solutions[] / diagnoses[] / verification_results[]`；`data/manifest.json` 汇总。
- 新增 `candidate_solution` 结构（id / trace_id / language / standard / source_code / execution_status）。
- 修正 `verification_result` 结构（solution_id / test_id / actual_output / verdict / runtime_ms / finding_ref），并写入『无执行则 verdict=not_run、actual_output/runtime_ms=null、不填虚构结果』规则。
- 为 `reasoning_trace` 增加生成与审查元数据：`trace_origin`（固定 model_generated）、`generator_model`（hy3）、`annotator`（hy3_draft）、`review_status`（pending_planner_review）、`reviewer`（null）、`reviewed_at`（null）。
- `reference_verdict` 增加 `common_wrong_strategy_counterexample` 字段。

## 8. 停止声明

本阶段在 Phase 1A 内停止：已完成数据格式冻结与首批 9 条样本；**不**开始 C++ 解析器（Phase 1B 起），**不**进入 Phase 2 评测管线。

## 9. 本轮审查修订记录（Phase 1A 限定）

针对首批样本做了一轮**限定审查修订**（仍停留在 Phase 1A：不进入 Phase 1B / Phase 2，不实现 / 修改 C++ 代码，不运行候选代码，不使用 Python，不连接 OJ，不改任何数据记录的 `review_status` / `reviewer` / `reviewed_at`）：

1. **无代码不得声称实现一致**：9 条轨迹中此前除 `cf_160A_t3`（关联 `cf_160A_sol1`）外，其余 8 条均含 `implementation_consistency` 步骤并断言“思路与代码 / 实现一致”。本轮已删除这 8 条轨迹的 `implementation_consistency` 步骤，仅保留 `cf_160A_t3`（其确实关联 `candidate_solution`）；并在 `data-contract.md` 第 2 节明确：`candidate_solution` 缺失时该环节必须省略、不得凭空断言一致、缺少可选代码不使纯思路轨迹自动判错。
2. **545D 正确性证明加强**：同步修订 `cf_545D.reference_verdict.expected_proof` 与 `cf_545D_t1` / `cf_545D_t3` 的 `greedy_proof`——给出“最优可调整为不失望前缀”的引理、升序扫描维护 S 的贪心构造、保持当前人数下累计 S 最小的不变量归纳，以及“若最小可选 t 都不满足 S ≤ t 则无法再增 1 人”的判定；未使用“交换后 a 更难但整体自然不减”作为唯一证明。`cf_545D_t3` 仍仅保留 `complexity_error`，未被改成证明错误轨迹。
3. **1398B 正确性证明加强**：同步修订 `cf_1398B.reference_verdict.expected_proof` 与 `cf_1398B_t1` 的 `greedy_proof`——补全“不删纯 0 段”“删 1 段的一部分被删整段支配”“归约为轮流取完整段”“每回合取最大段（交换论证）”“段长降序 Alice 取奇数位”五点逻辑。`cf_1398B_t2`（缺失证明）与 `cf_1398B_t3`（故意错误策略 + 多 findings）**未修复**，保留为负样本。
4. **README 同步**：将“首批人工审查样本”改为“首批待审模型生成样本”；增加项目性质声明（个人开源 / 参赛项目，非腾讯、非混元、非 Codeforces 官方仓库，不代表官方立场或背书），并记录计划公开地址 <https://github.com/Smily2333/hy3-algotrace>；更新目录树（列出 `manifest.json`、三个问题 JSON 与 `phase-01a.md`），修正“`data/problems` 为空预留目录”“Phase 0 为本阶段”等过期描述；明确本批题面直接来自 Codeforces 官方页面，CodeContests Verified 仅为后续候选来源而非本批实际来源。
5. **审查状态语义澄清**：在 `data-contract.md` 新增审查状态语义——`model_generated` 创建时为 `pending_planner_review`，可转 `planner_reviewed` 但不等同 `human_reviewed` / `expert-reviewed`；本轮未修改任何数据记录的 `review_status` / `reviewer` / `reviewed_at`。

上述 §9 描述的是审查修订轮次本身（该轮刻意保持 9 条样本为 `pending_planner_review`，未改任何审查元数据）。其后的最终验收见 §10：2026-08-23 已通过 codex_planner 复核，9 条样本审查元数据更新为 `planner_reviewed`。

## 10. 最终验收记录（Phase 1A 限定，仅更新审查元数据）

- **时间**：2026-08-23
- **复核方**：`codex_planner`（Codex 规划方技术复核），记录在每条样本的 `reviewer` 字段。
- **状态变更**：9 条推理轨迹的 `review_status` 由 `pending_planner_review` 更新为 `planner_reviewed`，`reviewed_at` 写 `2026-08-23`；`data/manifest.json` 同步置 `review_status = planner_reviewed`、`reviewer = codex_planner`、`reviewed_at = 2026-08-23`。
- **性质澄清**：本次 `planner_reviewed`（codex_planner 技术复核）**不等同于** `human_reviewed` 或 `expert-reviewed`，仅为规划方技术验收，尚未经过人工或专家审查背书。
- **范围严格性**：本次**只更新审查元数据**，未修改任何样本内容——未改动题面、参考证明、`diagnosis`、测试用例、计数或版本号；未实现 / 修改任何 C++ 文件；未使用 Python；未运行候选代码；未连接或提交 OJ；未推送 GitHub。
- **下一步**：样本已通过规划方技术复核，后续如需进入 `human_reviewed` / `expert-reviewed` 或 Phase 1B（C++ 解析器），须另行授权与执行。
