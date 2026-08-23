# Phase 2A Journal — 离线评估协议与 Prompt 模板冻结

> 阶段：`phase2a_complete_planner_reviewed`（codex_planner 技术验收通过，2026-08-24）
> 日期：2026-08-24
> 上游：Phase 1B（commit `fb40cb2`，tag `v0.3.0-phase1b`，`phase1b_complete_ci_verified`）
> 本阶段性质：**只设计协议，不评估、不实现、不调用 API。**

---

## 1. 为什么先做 reference_assisted

- 研究问题聚焦「参考辅助下的推理过程审查能力」，这是最贴近实际应用（人工审查员手边有参考答案）的场景，信号最干净。
- 先冻结 `reference_assisted` 能把「模型能否识别错误」与「模型能否独立解题」两个混淆变量分离；`closed_book` 留作对照，避免一开始就把两种能力混在一起评。
- 参考数据（reference_verdict / test_cases）是合法输入，不视为标签泄漏，协议 denylist（见 `docs/phase-02-protocol.md` 第 3 节）只对 gold diagnosis 与审查元数据做隔离。

## 2. 为什么不先做 CandidateRunner

- 初版 CandidateRunner 设计为**本地受限**版本：本地编译、超时运行、标准输入输出对比，产出 `verification_result`，用于 `implementation_consistency` 的实证信号。**本地 CandidateRunner 不依赖 OJ**；主要风险是安全隔离、资源限制与平台差异，而非外部服务可用性。
- OJ 对接属未来可选扩展，须另行授权，不是 Phase 2D 初版默认内容。
- 初版目标是冻结「过程审查」协议与 Prompt；代码验证属独立职责线（`code_test_verification`，见 `architecture.md`），放在 Phase 2D 扩展更清晰，避免初版范围膨胀。
- 过早引入执行会放大标签泄漏风险（执行结果本身可能直接揭示正确性）。

## 3. 为什么不直接接模型 API

- 本阶段目标是「协议与契约冻结」，需要规划方先审视 allowlist/denylist、输出契约、指标定义是否公平、可复现、无泄漏。
- 先接 API 会把「协议设计是否正确」与「模型表现如何」耦合，一旦协议有缺陷，实验结论不可信且难以追责。
- 协议明确：初版只做离线模型适配（prompt 渲染规则 + 原始响应保留）。Phase 2C 初版采用用户在 Codex 与 WorkBuddy/Hy3 之间转交 Prompt 和响应的**离线交互流程**；Phase 2C **不默认实现或调用**模型 API；API 接入是未来可选扩展，必须另行授权。

## 4. 为什么保存原始 prompt / response

- 原始 prompt 是复现实验的唯一依据；其 SHA-256 写入 `run-manifest` 与 `predictions`，保证任何人可用同一模板 + 同一数据复现。
- 原始 response 是审计源头：模型可能输出非 JSON、部分 JSON 或带前缀文字；必须逐字保留，不得只存清洗后 JSON，否则丧失排查与（未来）repair 分析的可能。
- 协议第 9 节明确「不允许静默修复」：修复若引入，必须作为独立步骤与独立指标。

## 5. 标签泄漏风险（R1 修订）

- **高风险字段**（必须隔离）：`diagnoses`（整段 gold）、`status` / `primary_category` / `findings` gold label、`manifest.category_counts` / `status_counts`、`review_status` / `reviewer` / `reviewed_at`、`trace_origin` / `generator_model` / `annotator`。
- **R1 新增——`test_cases.notes` 属语义泄漏**：规划方检查真实数据后确认，`notes` 中存在直接点名目标 trace、直接写出 gold category（如 `wrong_greedy_choice`、`boundary_omission`）、或明确声明「击破某条错误轨迹」的内容。v1 Prompt 的 `test_cases` allowlist **已排除 `notes`**（仅保留 `id` / `input` / `expected_output` / `origin` / `purpose`）；`purpose = counterexample` 可保留（描述用途，不给当前轨迹 gold label）；`reference_verdict.common_wrong_strategy_counterexample` 可保留（属 reference_assisted 合法参考）。
- **泄漏分层**（修正原「扫描整个 prompt」不可行方案）：
  - **structural leakage**：禁止字段进入输入结构 → 由 PromptExporter 显式 allowlist 构造输入 JSON + 递归 key 检查（拦截 `diagnoses` / `review_status` / `reviewer` / `reviewed_at` / `trace_origin` / `generator_model` / `annotator`）。
  - **semantic leakage**：自由文本（如 `notes`、文件名、预先说明）直接透露目标 trace gold 标签 → v1 通过排除 `test_cases.notes` 降低风险；后续若允许其他自由文本字段，须单独人工或规则审计。
  - **不得**在整个 prompt 上搜索 `status` / `correct` / 7 类 category 名称等通用字符串——这些在输出契约中合法出现，搜索必误报。leakage audit 只检查输入 payload 区域。
- **隐蔽泄漏渠道**：文件名/目录名编码正确与否（如 `trace_correct_01`）、预先说明「本条轨迹经过复核」、跨轨迹透露其他诊断。
- **缓解**：allowlist 显式构造 + 输入 payload 递归 key 检查；`trace.id` 可保留但不得编码 label。

## 6. 9 条样本的规模限制

- 数据：3 题（cf_160A / cf_545D / cf_1398B）× 3 轨迹 = 9 条，均 `algorithm_type = greedy`。
- 该规模仅够做冒烟实验（验证协议可跑通、可解析、可比较），**任何率值都不具统计显著性**。
- 结论不得外推为「Hy3 总体能力」；详见 `docs/phase-02-metrics.md` 第 12 节。

## 7. 当前未决问题（open questions）

1. **后续是否增加 `closed_book` 对照**：需规划方确认是否以及如何定义 `closed_book` Prompt，以及对照实验设计（同 9 条样本还是新样本）。
2. **finding 文本语义相似度如何评估**：当前指标只看类别/环节集合匹配，未评估 `locating` / `evidence` / `suggestion` 的语义近似；是否引入文本相似度（如 embedding 余弦）或纯人工定性，待定。
3. **人工评分由谁完成**：第 11 节人工定性审查（evidence/suggestion/root_cause quality）的评审员身份、资质与评分标准版本需明确。
4. **是否在 Phase 2C 前增加更多 `candidate_solution`**：当前仅 1 条候选解法，影响 `implementation_consistency` 环节的可评估样本量；是否补充待规划方决定。
5. **后续模型版本不可得时如何记录**：`model_version` 初版固定 `null`；若未来 Hy3 返回版本标识，如何回填与在 `run-manifest` 中追溯，需约定。

## 8. 明确未实际运行（R1 修正指标表述）

- **本阶段没有实际运行 Hy3 评测，且没有创建任何实验 run**：因此不存在 `parse_status` 记录，全部指标为 `N/A` / `not_computed`。**不得**报告「9 条轨迹 parse_status = model_call_not_attempted」或「parse_success_rate = 0」——这些只有在创建 run 并把轨迹加入后才成立。
- 未生成 9 条 prompt 文件（仅冻结模板与渲染规则，模板已用 `<!-- HY3_PROMPT_BEGIN -->` / `<!-- HY3_PROMPT_END -->` 标记真正交付区域）。
- 未创建 `experiments/phase-02/runs/<run_id>/`。
- 未实现任何 C++（ProcessEvaluator / Reporter / PromptExporter / PredictionImporter）。
- 未修改数据契约 0.3.0 / taxonomy 1.0.0。
- 未提交、未打 tag、未推送。
- 未进入 Phase 2B。

## 9. R1 协议审查修订摘要（2026-08-24）

规划方复审提出 9 项修订，均已落实于 `docs/phase-02-protocol.md` / `docs/phase-02-metrics.md` / `prompts/hy3-evaluator-v1.md` / 本文件 / `docs/architecture.md`：

1. **test_cases.notes 排除**：allowlist 移除 `notes`，降低 semantic leakage（仅过滤投影，不修改原始 data）。
2. **leakage audit 重写**：显式 allowlist 构造 + 输入 payload 递归 key 检查（structural），不扫描通用字符串；区分 structural / semantic leakage。
3. **解析失败指标处理**：parse 失败 macro-F1 固定 0、不套用「空对空=1」；micro 用内部 sentinel `__parse_failed__`，不写回 prediction。
4. **Phase 2A 指标表述**：无 run 时一律 `N/A`，不报 0%。
5. **wrapper / run-manifest nullability**：`completed_at` / `model_version` / `prompt_sha256` / `raw_response_sha256` / `prediction` / `generated_at` 明确联合类型与取值规则；新增 `pipeline_commit`；`trace_ids` 字典序稳定排序。
6. **Prompt 规范边界与哈希**：BEGIN/END 标记；`prompt_template_sha256`（模板）与 `prompt_sha256`（实例）分别定义；UTF-8 无 BOM、LF 规范化。
7. **undetermined 收紧**：遗漏证明/复杂度/边界属可确认错误，不是 undetermined；undetermined 仅用于输入损坏/截断/真实歧义；`correct` 改为「所有适用环节正确」。
8. **实现边界措辞**：`JSON.parse` → 「标准 JSON 解析器（nlohmann/json）」；Phase 2C 离线不默认调 API；Phase 2D 本地不连 OJ；修正 confidence 章节引用。
9. **journal 同步**：CandidateRunner 改为本地受限、不依赖 OJ。

Phase 2A-R1.2 收口后状态仍为「待规划方复审」的 draft 态，等待最终验收。

## 9. 本阶段产出文件

- `docs/phase-02-protocol.md` — 协议（研究问题、模式、allowlist/denylist、流程、失败状态、运行目录、prediction wrapper）
- `docs/phase-02-metrics.md` — 指标定义
- `prompts/hy3-evaluator-v1.md` — 可复用 Prompt 模板
- `docs/journal/phase-02a.md` — 本文件
- 同步：`README.md`、`docs/roadmap.md`、`docs/architecture.md`

## 10. 规划方最终验收（2026-08-24）

- **reviewer**：`codex_planner`
- **reviewed_at**：`2026-08-24`
- **result**：`accepted`
- **最终状态**：`phase2a_complete_planner_reviewed`
- **性质说明**：本次为 codex_planner **技术验收**，仅代表规划方技术复核通过，**不等同于**人工（human_reviewed）或专家（expert-reviewed）审查背书。
- **未做之事（明确边界）**：
  - 尚未运行 9 条轨迹的 Hy3 模型实验；
  - 所有 Phase 2A 指标仍为 `N/A` / `not_computed`（无 run、无 prediction）；
  - 未进入 Phase 2B。
- **审查问题闭环记录**：R1（9 项协议审查修订）、R1.1（Phase 2C/2D 边界与 micro 零分母一致性收口）、R1.2（Phase 2C API 表述单点修正）均已解决并通过规划方实际仓库核验。
- **下一步**：等待规划方授权后创建 Phase 2A tag（`v0.4.0-phase2a` 候选，待定）；进入 Phase 2B（C++ PromptExporter / PredictionImporter / Reporter）需另行授权。
