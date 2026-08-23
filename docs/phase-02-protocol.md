# Phase 2A — Hy3 离线推理过程评估协议与 Prompt 模板冻结

> 阶段状态：**`phase2a_complete_planner_reviewed`**（codex_planner 技术验收通过，2026-08-24；不等同 human/expert review）
> 上游基线：Phase 1B（commit `fb40cb2`，tag `v0.3.0-phase1b`，状态 `phase1b_complete_ci_verified`）
> 数据契约：`0.3.0`　错误分类：`taxonomy 1.0.0`　Phase 1A 数据逐字节不变
> 本阶段**只设计实验协议、输出契约、评价指标与可复现 Prompt，不编写 C++、不实际评估 9 条轨迹、不调用模型 API、不运行候选代码、不连接 OJ、不使用 Python。**
> 配套文件：`prompts/hy3-evaluator-v1.md`（Prompt 模板）、`docs/phase-02-metrics.md`（指标）、`docs/journal/phase-02a.md`（设计记录）。

---

## 1. 研究问题

初版只研究一个具体、可复现的问题：

> **在给定题目、参考结论、测试用例和待审查推理轨迹的情况下，Hy3 能否正确判断算法竞赛（贪心题）解法思路、复杂度与边界处理，并定位错误类型？**

关键限定：

- 初版模式固定为 **`reference_assisted`**（参考辅助下的推理过程审查能力），**不是**闭卷独立解题能力。
- `closed_book` 模式只作为后续扩展记录（见第 11 节），不在本阶段实现、不定义其 Prompt。
- 初版**只处理 `algorithm_type = greedy`**，不自动识别算法类型（TypeRouter 属 Phase 6）。
- 研究结论的适用范围受样本规模限制（见 `docs/journal/phase-02a.md` 与 `docs/phase-02-metrics.md` 第 12 节）。

---

## 2. reference_assisted 输入模式

`reference_assisted` 模式下，Hy3 可以看到题目、参考结论与测试用例，用来对照轨迹的逻辑一致性。这是合法的「开卷审查」，不视为标签泄漏（见第 3 节 denylist 对 gold diagnosis 的隔离）。

该模式评估的是：

- 给定充分上下文时，模型能否**正确识别**轨迹思路中的矛盾、证明缺口、复杂度或边界错误；
- 能否**准确定位**错误发生在六环节的哪一环节、属于 7 类错误的哪一类；
- 能否避免把「措辞不同但逻辑等价」误判为错误。

它与 `closed_book`（无参考、独立判断）形成对照，后者留待后续扩展，用于隔离「参考信息本身是否泄漏了答案」这一混淆变量。

---

## 3. 输入可见性与标签泄漏规则

### 3.1 allowlist（Hy3 初版允许看到的字段）

Hy3 在 `reference_assisted` 模式下，可以接收以下字段（按 JSON 路径组织）：

1. **`problem`**
   - `id`
   - `source`
   - `title`
   - `statement`
   - `constraints`
   - `algorithm_type`
   - `reference_tags`

2. **`reference_verdict`**
   - `expected_choice`
   - `expected_proof`
   - `expected_complexity`
   - `expected_boundaries`
   - `common_wrong_strategy_counterexample`

3. **`test_cases`**（数组，每条）
   - `id`
   - `input`
   - `expected_output`
   - `origin`
   - `purpose`

   > **排除 `test_cases.notes`**：真实数据中 `notes` 可能直接点名目标 trace、写出 gold category（如 `wrong_greedy_choice`、`boundary_omission`）或声明「击破某条错误轨迹」，属 semantic leakage，不得进入 prompt。`purpose = counterexample` 可保留（描述测试用途，不直接给出当前轨迹 gold label）；`reference_verdict.common_wrong_strategy_counterexample` 可保留（属 reference_assisted 合法参考）。PromptExporter 的输入投影规则**显式**按上面 5 个字段构造，不复制整个源对象后再删字段。

4. **`reasoning_trace`**
   - `id`
   - `problem_id`
   - `author`
   - `steps`
   - `intended_outcome`（若数据集确实存在该字段）

5. **`candidate_solution`**（仅当该 trace 确实有关联候选代码时提供）
   - `id`
   - `trace_id`
   - `language`
   - `standard`
   - `source_code`
   - `execution_status`
   - 必须明确：`execution_status = not_run` **不代表**代码通过；`passed` 也只表示记录状态，Hy3 未执行验证。

### 3.2 denylist（Hy3 初版不得看到的字段 / 信息）

以下字段属于 gold label 或审查元数据，**必须完全从渲染后的 prompt 中剔除**，不得通过任何形式（字段名、注释、文件名、目录名或预先说明）向 Hy3 透露：

- `diagnoses`（整段 gold diagnosis，含 status / primary_category / findings）
- `status` gold label
- `primary_category` gold label
- `findings` gold label
- `manifest.category_counts`
- `manifest.status_counts`
- `review_status`
- `reviewer`
- `reviewed_at`
- `trace_origin`
- `generator_model`
- `annotator`
- 数据集中**其他轨迹**的诊断
- 任何根据文件名、目录名或预先说明**直接暗示该轨迹正确 / 错误**的信息
- `test_cases.notes`（自由文本，可能直接写出 gold category 或点名目标 trace，见 3.1 第 3 项）

### 3.3 标签泄漏规则说明

- `trace.id` 可以保留用于关联（渲染后的 prompt 与 prediction 都带它），但**不得编码** gold label（如不得命名为 `trace_correct_01`）。
- 参考答案（`reference_verdict`）与测试用例（`test_cases`，不含 `notes`）属于 `reference_assisted` 模式的**合法输入**，不视为泄漏。
- gold diagnosis 是**评价目标**，必须完全隔离；Hy3 输出只在 Reporter 阶段与 gold 比较。

### 3.4 leakage audit 实现要求（修正版，Phase 2B 实现）

原「扫描整个渲染后 prompt 是否出现 denylist 字段名或 gold 值」**不可实现**，因为 Prompt 输出契约本身合法包含 `status` / `primary_category` / `findings` / `correct` / `incorrect` / 7 类 category 名称。改为以下分层策略：

1. **PromptExporter 必须通过显式 allowlist 构造新的输入 JSON**，不得复制整个源对象后再删除字段（见 3.1）。
2. **leakage audit 只检查「输入 payload 区域」**（BEGIN/END 之间替换占位符后的输入 JSON 块），**不检查**模板的任务说明与输出 schema 区域。
3. 对输入 payload 做**递归 key 检查**，禁止以下 key 出现在任何层级：
   - `diagnoses`
   - `review_status`
   - `reviewer`
   - `reviewed_at`
   - `trace_origin`
   - `generator_model`
   - `annotator`
4. **不得**在整个 prompt 上简单搜索 `status`、`correct`、category 名称等通用字符串（这些在输出契约中合法出现，搜索会误报）。
5. **Phase 2B 测试应验证**：
   - 源数据含 `diagnoses`，但投影结果不含；
   - `test_cases.notes` 不进入 payload；
   - 模板输出契约中出现 `status`/`findings` 不会造成误报；
   - 输入 payload 中出现禁止 key 时会失败。

区分两类泄漏：

- **structural leakage**：禁止字段（3.4.3 列表）进入输入结构 → 由递归 key 检查拦截。
- **semantic leakage**：自由文本（如 `notes`、文件名、预先说明）直接透露目标 trace 的 gold 标签 → v1 通过排除 `test_cases.notes` 降低风险；后续若允许其他自由文本字段，必须单独做人工或规则审计（不在 v1 自动范围内）。

---

## 4. 单条轨迹的完整处理流程

每条轨迹从冻结数据到报告，遵循以下确定性流程（C++ 实现属 Phase 2B/2C，本阶段只定义）：

1. **从冻结数据读取**：按 `data/manifest.json` + `data/problems/<id>.json` 加载（数据契约 0.3.0，逐字节不变）。
2. **过滤禁止字段**：依据第 3 节 denylist，剔除 gold label 与审查元数据，只保留 allowlist 字段。
3. **渲染 prompt**：将过滤后的字段填入 `prompts/hy3-evaluator-v1.md` 的 `<!-- HY3_PROMPT_BEGIN -->` / `<!-- HY3_PROMPT_END -->` 标记之间模板文本的 5 个占位符，生成纯文本 prompt（标记本身不发送）。
4. **保存 prompt**：写入 `experiments/phase-02/runs/<run_id>/prompts/<trace_id>.txt`，并记录其 SHA-256。
5. **交给 Hy3**：离线推理（人工复制 prompt 至 Hy3 / WorkBuddy，或后续脚本）。本阶段**不调用 API**。
6. **保存 raw response**：将 Hy3 的原始回复**逐字**写入 `raw-responses/<trace_id>.txt`，记录 SHA-256。**原始响应永远保留，不得只保存清洗后的 JSON。**
7. **解析 JSON**：使用标准 JSON 解析器（Phase 2B C++ 使用 `nlohmann/json`）解析 raw response；记录 `parse_status`。
8. **schema 校验**：检查必需键、枚举值、类型（见 `docs/data-contract.md` 第 6 节 diagnosis 结构 + 本文件第 7 节 wrapper）。
9. **语义校验**：检查 `status` 与 `primary_category` / `findings` 的约束一致性（见第 5 节语义规则）。
10. **与 gold diagnosis 比较**：仅在 Reporter 阶段内存读取 gold，或进入 `report.json` / `report.md`；**gold 绝不能写进 `predictions/<trace_id>.json`**。
11. **生成报告**：汇总指标（见 `docs/phase-02-metrics.md`），输出 `report.json` 与 `report.md`。

---

## 5. 失败状态（parse_status）

每条轨迹的解析/校验结果记为以下之一：

| parse_status | 含义 |
| --- | --- |
| `model_call_not_attempted` | 尚未交给 Hy3（仅在已创建 run 且未调用模型时出现；Phase 2A 尚未创建任何 run，故无此记录） |
| `empty_response` | Hy3 返回空文本 |
| `invalid_json` | 返回非空但无法解析为 JSON |
| `schema_invalid` | JSON 合法但缺键 / 类型错 / 枚举非法 |
| `semantic_invalid` | schema 通过但 `status` 与 `primary_category`/`findings` 约束冲突 |
| `parsed` | 通过 schema + 语义校验，可作为 prediction |

说明：

- 原始响应永远保留（第 4 节第 6 步），即使 `parse_status != parsed`。
- **Phase 2A 没有实验 run**：所有指标在 Phase 2A 均为 `N/A` / `not_computed`，不得因为「尚未进行实验」就报告 `parse_success_rate = 0` 或声称 9 条轨迹属于 `model_call_not_attempted`。只有未来创建 `run-manifest` 并把轨迹加入该 run 后，未调用模型的轨迹才记录 `model_call_not_attempted`。
- **不允许静默修复模型输出**。若未来加入 repair pass，必须作为**独立步骤**并作为**独立指标**记录（如 `repaired_count`），不得把修复后的 JSON 伪装成原始 parsed 结果。本阶段不定义 repair pass。

---

## 6. 实验运行目录（Phase 2B/2C 使用）

本阶段只定义，不实际创建 run，不生成 9 条 prompt。

```text
experiments/phase-02/runs/<run_id>/
├── run-manifest.json
├── prompts/
│   └── <trace_id>.txt
├── raw-responses/
│   └── <trace_id>.txt
├── predictions/
│   └── <trace_id>.json
├── report.json
└── report.md
```

### 6.1 run-manifest.json（至少记录）

```json
{
  "evaluation_schema_version": "0.1.0",
  "run_id": "string",
  "dataset_version": "phase1a-pilot-001",
  "dataset_commit": "fb40cb2f8f93967a93a376508c5a0d9c3f3f4df9",
  "taxonomy_version": "1.0.0",
  "model_provider": "tencent-hunyuan",
  "model_name": "hy3",
  "model_version": null,
  "pipeline_commit": "string",
  "prompt_template_id": "hy3-evaluator-v1",
  "prompt_template_sha256": "string",
  "input_mode": "reference_assisted",
  "started_at": "ISO-8601",
  "completed_at": "ISO-8601 或 null（运行未完成时）",
  "trace_ids": ["string（字典序稳定排序）"],
  "total_traces": 9,
  "notes": "string"
}
```

字段 nullability 与约束：

- `model_version`：`string | null`，模型版本不可得时为 `null`。
- `pipeline_commit`：`string`，固定 `PromptExporter` / `PredictionImporter` / `Reporter` 实现版本（git commit），用于复现。
- `started_at`：`string`（ISO-8601）。
- `completed_at`：`string | null`，运行未完成时为 `null`。
- `trace_ids`：`string[]`，**必须按字典序稳定排序**（保证 run-manifest 可复现、diff 稳定）。
- 不得用字面量 `"string|null"` 作为真实值；联合类型只在说明中表达。

> **`evaluation_schema_version`（0.1.0）是评估协议版本，与数据 `schema_version`（0.3.0）是两个独立维度，不得混为一谈。** 数据契约升级不自动改变评估协议版本，反之亦然。

初版建议值：

- `evaluation_schema_version = "0.1.0"`
- `prompt_template_id = "hy3-evaluator-v1"`
- `input_mode = "reference_assisted"`
- `model_version`：若模型版本不可得则为 `null`（见 `docs/journal/phase-02a.md` 未决问题）。

---

## 7. prediction wrapper（predictions/\<trace_id\>.json）

保存到 `predictions/<trace_id>.json` 的包装结构：

```json
{
  "evaluation_schema_version": "0.1.0",
  "run_id": "string",
  "trace_id": "string",
  "model_name": "hy3",
  "prompt_template_id": "hy3-evaluator-v1",
  "input_mode": "reference_assisted",
  "prompt_sha256": "string 或 null（prompt 尚未生成时）",
  "raw_response_sha256": "string 或 null",
  "parse_status": "model_call_not_attempted|empty_response|invalid_json|schema_invalid|semantic_invalid|parsed",
  "prediction": { /* parsed 时结构见下方；其他状态必须为 null */ } 或 null,
  "errors": ["string"],
  "generated_at": "ISO-8601 或 null"
}
```

`prediction` 内部结构与 `prompts/hy3-evaluator-v1.md` 输出契约一致：

```json
{
  "trace_id": "string",
  "status": "correct|incorrect|undetermined",
  "primary_category": "string|null",
  "findings": [],
  "confidence": null,
  "confidence_method": null,
  "calibration_version": null
}
```

字段 nullability 与约束：

- `prompt_sha256`：`string`；prompt 尚未生成时为 `null`。
- `raw_response_sha256`：
  - `model_call_not_attempted` 时为 `null`；
  - `empty_response` 时为「实际保存的空文件字节」的 SHA-256；
  - 其他状态为原始响应文件字节的 SHA-256。
- `prediction`：`parsed` 时必须非 `null`；其他状态必须为 `null`。
- `errors`：`array<string>`，记录 schema / 语义校验失败的具体原因（便于 Phase 2C 排查）。
- `generated_at`：`string | null`，wrapper 生成时间；尚未生成时为 `null`。
- 不得用字面量 `"string|null"` 作为真实值；联合类型只在说明中表达。

说明：

- `prediction` 是 Hy3 输出解析后的结构；其形状与 `prompts/hy3-evaluator-v1.md` 输出契约一致。
- **gold diagnosis 绝不能写进 prediction 文件。**
- comparison 只在 Reporter 阶段内存读取 gold，或进入 `report.json` / `report.md`。
- raw response 必须独立保存于 `raw-responses/<trace_id>.txt`，并用 `raw_response_sha256` 关联。
- `parse_status != parsed` 时 `prediction` 必须为 `null`（Phase 2A 尚未创建 run，故无任何 wrapper 文件）。
- `__parse_failed__` 等内部 sentinel 只用于指标计算（见 `docs/phase-02-metrics.md` 第 3 节），**绝不写回** prediction JSON。

---

## 8. Hy3 输出契约（与 Prompt 模板一致）

输出必须为单个 JSON 对象，无 Markdown 代码围栏、无解释性前言：

```json
{
  "trace_id": "string",
  "status": "correct|incorrect|undetermined",
  "primary_category": "7类之一或null",
  "findings": [
    {
      "stage": "六环节之一",
      "category": "7类之一",
      "locating": "具体定位",
      "evidence": "判定依据",
      "suggestion": "改进建议"
    }
  ],
  "confidence": null,
  "confidence_method": null,
  "calibration_version": null
}
```

语义约束（强制）：

- `correct`：`primary_category = null`，`findings = []`。
- `incorrect`：`primary_category` 非 null，`findings` 非空，且 `primary_category` 必须出现在某条 `findings[].category` 中。
- `undetermined`：`primary_category = null`，`findings = []`，仅在输入信息确实不足以可靠判断时使用。
- 不得把「缺少贪心证明」误标为 `undetermined`；应证明而未证明 → `missing_greedy_proof`。
- `confidence` / `confidence_method` / `calibration_version` 在 Phase 4 前固定为 `null`。

---

## 9. 冻结 Prompt 的规范边界与哈希

`prompts/hy3-evaluator-v1.md` 同时含设计说明与真正 Prompt。Phase 2B 必须明确：只渲染 `<!-- HY3_PROMPT_BEGIN -->` 与 `<!-- HY3_PROMPT_END -->` 之间的内容交给 Hy3；两个标记本身不发送；手工复制时也只复制该区域。

哈希约定（用于 `run-manifest.prompt_template_sha256` 与 `prediction.prompt_sha256`）：

- **统一文本规范**：UTF-8、无 BOM；CRLF/CR 统一规范化为 LF 后再计算 SHA-256。
- **`prompt_template_sha256`**：对 BEGIN/END 之间、**替换占位符前**的模板文本计算（即冻结模板本身，与具体数据无关）。
- **`prompt_sha256`**：对替换占位符后的**完整 Prompt 文本**计算（同样 UTF-8、无 BOM、LF 规范化）。
- **`raw_response_sha256`**：对实际保存的原始响应文件字节计算，不做任何文本修复或规范化。

> 这样 `prompt_template_sha256` 标识「用了哪个模板版本」，`prompt_sha256` 标识「实际发给模型的那段文本」，二者可在复现时分别核对。

---

## 10. 原始响应保留与不可修复原则

- 原始响应（`raw-responses/<trace_id>.txt`）是审计源头，**永远保留**，不得只保存清洗后的 JSON。
- 不得静默修复模型输出。未来若引入 repair pass，必须：
  - 作为独立步骤（在 parse 之后、比较之前）；
  - 作为独立指标记录（如 `repaired_count`、`repair_success_rate`）；
  - 明确区分「原始 parsed」与「修复后 parsed」，不覆盖原始记录。

---

## 11. 初版范围硬性边界

1. 初版只处理 `algorithm_type = greedy`，**不自动识别类型**（TypeRouter 属 Phase 6）。
2. 初版只做**离线模型适配**，不调用模型 API。Phase 2C 初版仍采用用户在 Codex 与 WorkBuddy/Hy3 之间转交 Prompt 和响应的离线交互流程；API 接入仅作为未来可选扩展，必须另行授权。
3. 初版**不执行候选代码**（CandidateRunner 属 Phase 2D，且初版只做本地受限版本）。
4. `closed_book` 模式、`CandidateRunner`、`TypeRouter` 属后续扩展，**不混入初版**。

---

## 12. 后续扩展记录（不在本阶段实现）

- **`closed_book` 模式**：不向 Hy3 提供 `reference_verdict` 与 `test_cases`，用于隔离参考信息是否泄漏答案；需单独定义 Prompt 与对照实验。
- **CandidateRunner（Phase 2D，本地受限）**：本地编译、超时运行、标准输入输出对比，产出 `verification_result`，用于 `implementation_consistency` 的实证信号。**初版不连接或提交外部 OJ**；OJ 对接属于未来可选扩展，须另行授权。
- **TypeRouter（Phase 6）**：自动识别算法类型，替代人工 `greedy` 标注透传。

---

## 13. 本阶段未做之事（明确边界）

- **未实际运行** Hy3 对 9 条轨迹的评测；Phase 2A **没有创建任何 run**，故无 `model_call_not_attempted` 记录、无 prediction wrapper。
- 未生成 9 条 prompt 文件（仅定义渲染规则与模板）。
- 未创建 `experiments/phase-02/runs/<run_id>/` 目录。
- 未实现任何 C++（ProcessEvaluator / Reporter / PromptExporter / PredictionImporter）。
- 未修改数据契约 0.3.0 / taxonomy 1.0.0。
- 未提交、未打 tag、未推送。
- 未进入 Phase 2B。

> 指标表述修正：Phase 2A 所有指标为 `N/A` / `not_computed`，**不得**报告 `parse_success_rate = 0` 或「9 条轨迹 parse_status = model_call_not_attempted」——这些只有在创建 run 后才成立。
