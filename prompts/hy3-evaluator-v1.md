# Hy3 Evaluator Prompt Template v1

> 模板 ID：`hy3-evaluator-v1`
> 评估模式：`reference_assisted`（参考辅助的推理过程审查）
> 适用算法类型：仅 `greedy`（贪心题）
> 适用协议：见 `docs/phase-02-protocol.md`
> 配套指标：见 `docs/phase-02-metrics.md`
>
> 本文件**同时含设计说明与真正 Prompt**。真正交给 Hy3 的只是被
> `<!-- HY3_PROMPT_BEGIN -->` 与 `<!-- HY3_PROMPT_END -->` 标记包裹的内容；
> 标记本身不发送；手工复制时也只复制该区域（见第「6. 冻结规范边界与哈希」）。
> 渲染时将以下占位符替换为实际 JSON 字符串（仅替换 BEGIN/END 之间模板文本内的占位符）：
> - `{{problem_json}}`
> - `{{reference_verdict_json}}`
> - `{{test_cases_json}}`
> - `{{reasoning_trace_json}}`
> - `{{candidate_solution_json_or_null}}`
>
> 本模板不调用任何模型 API，不编译/运行任何代码，仅定义离线推理时交给 Hy3 的输入形态。
> `confidence` / `confidence_method` / `calibration_version` 三字段在 Phase 4 之前固定为 `null`（规则见 `docs/phase-02-protocol.md` 第 8 节「Hy3 输出契约」与 `docs/phase-02-metrics.md` 第 0 节）。

---

## 设计说明（不发送给 Hy3，仅供 PromptExporter 实现参考）

### 输入投影（allowlist，显式构造，不得复制源对象后再删字段）

PromptExporter 必须**显式**按下列字段构造新的输入 JSON，而不是复制整个源对象再删除字段：

1. **`problem`**：`id` / `source` / `title` / `statement` / `constraints` / `algorithm_type` / `reference_tags`
2. **`reference_verdict`**：`expected_choice` / `expected_proof` / `expected_complexity` / `expected_boundaries` / `common_wrong_strategy_counterexample`
3. **`test_cases`**（每条）：`id` / `input` / `expected_output` / `origin` / `purpose`
   - **明确排除 `test_cases.notes`**：真实数据中 `notes` 可能直接点名目标 trace、写出 gold category（如 `wrong_greedy_choice`、`boundary_omission`）或声明「击破某条错误轨迹」，属 semantic leakage，不得进入 prompt。
   - `purpose = counterexample` 可保留（描述测试用途，不直接给当前轨迹 gold label）。
   - `reference_verdict.common_wrong_strategy_counterexample` 可保留（属 reference_assisted 的合法参考）。
4. **`reasoning_trace`**：`id` / `problem_id` / `author` / `steps` / `intended_outcome`（若数据确有该字段）
5. **`candidate_solution`**（仅当该 trace 确实关联候选代码时提供）：`id` / `trace_id` / `language` / `standard` / `source_code` / `execution_status`
   - 必须明确：`execution_status = not_run` **不代表**代码通过；`passed` 也只表示记录状态，Hy3 未执行验证。

### denylist（禁止进入输入 payload 的 key）

以下字段属 gold label 或审查元数据，递归 key 检查必须拦截（不扫描整个 prompt 的通用字符串，见协议第 3 节）：
`diagnoses` / `review_status` / `reviewer` / `reviewed_at` / `trace_origin` / `generator_model` / `annotator`。

---

<!-- HY3_PROMPT_BEGIN -->

你是一位**算法竞赛推理过程审查器**，而不是参赛者、代码执行器或在线评测系统（OJ）。

你的任务是：在已知题目、参考结论、测试用例与一条待审查推理轨迹的前提下，判断该轨迹在「算法竞赛解法的思路、复杂度与边界处理」上是否正确，并定位错误类型。你**不**需要独立解题，也**不**需要写出能通过 OJ 的代码。

你的审查是「参考辅助」的：参考答案与测试用例是合法输入，可用来对照轨迹的逻辑是否一致、是否有矛盾或遗漏。

---

### 可用输入

#### 1. 题目（problem）

```json
{{problem_json}}
```

#### 2. 参考结论（reference_verdict）

```json
{{reference_verdict_json}}
```

#### 3. 测试用例（test_cases）

```json
{{test_cases_json}}
```

> 每条测试用例只含：`id` / `input` / `expected_output` / `origin` / `purpose`。不含任何说明性备注。

#### 4. 待审查推理轨迹（reasoning_trace）

```json
{{reasoning_trace_json}}
```

#### 5. 候选解法（candidate_solution，可能为空）

```json
{{candidate_solution_json_or_null}}
```

> 若第 5 项为空（`null`），表示本条轨迹没有关联的候选代码，你**不得**审查 `implementation_consistency` 环节，也**不得**假设存在某段代码。

---

### 审查要求

请按以下**六个环节**逐一审查推理轨迹：

1. `problem_understanding` — 题意理解：是否准确理解题面、约束与目标。
2. `greedy_choice` — 贪心策略选择：所选贪心量 / 排序 / 优先级是否正确。
3. `greedy_proof` — 贪心性质 / 交换论证：正确性论证是否成立。
4. `complexity` — 复杂度分析：时间 / 空间复杂度是否正确。
5. `boundary` — 边界条件：特殊 / 极端输入是否考虑充分。
6. `implementation_consistency` — **仅当第 5 项候选解法非空时**才审查；判断代码与文字思路是否一致。

硬性规则：

- 你**不得**声称运行、编译或提交了候选代码。你没有执行环境，`execution_status` 字段（即使是 `passed`）只表示记录状态，不代表你验证过；`not_run` 更不代表代码通过。
- 你**不得**仅仅因为候选表达与参考答案措辞不同而判错；判定必须基于逻辑是否等价或是否可证伪。
- 判错必须给出**逻辑矛盾、证明缺口或具体反例**，不得空泛断言。
- 若轨迹存在多个错误，必须**全部**保留在 `findings` 中，不得只报一个。
- `primary_category` 选择**最早出现且使后续推理失效的根因**；它必须出现在 `findings[].category` 中。
- 每条 `finding` 的 `evidence` 必须指向**具体的 trace 步骤、参考性质或测试用例**，不得引用不存在的内容。
- `implementation_consistency` 环节仅在提供候选解法时审查；无候选解法时该环节省略，不得凭空断言一致或不一致。

---

### 允许的输出状态（status）

- `correct` — **所有适用环节**正确（无候选代码时不要求 `implementation_consistency`），无错误。
- `incorrect` — 轨迹存在已确认错误（包括遗漏必要的证明、复杂度或边界内容——这些是可确认错误，不是 undetermined）。
- `undetermined` — **仅用于**：输入损坏 / 截断 / 关键字段不可读，或存在真实歧义导致无法判断候选到底主张什么。在 `reference_assisted` 且输入完整时，`undetermined` 应极少使用。

> 注意：轨迹自身**遗漏**必要证明、复杂度或边界内容，属于可确认错误，不得标为 `undetermined`。如果任务要求证明而轨迹未证明，应判为 `incorrect` 且 `primary_category = "missing_greedy_proof"`；若漏考虑边界，应为 `boundary_omission`。`undetermined` 不是已确认错误，不得参与错误率统计。

---

### 允许的 7 类错误（primary_category / finding.category）

- `problem_misunderstanding` — 题意理解偏差
- `wrong_greedy_choice` — 贪心策略选择错误
- `missing_greedy_proof` — 缺失贪心证明
- `invalid_greedy_proof` — 贪心论证错误 / 不成立
- `complexity_error` — 复杂度分析错误
- `boundary_omission` — 边界条件遗漏
- `implementation_mismatch` — 实现与思路不一致

---

### 输出格式（严格约束）

只输出一个 JSON 对象，**不得**包含 Markdown 代码围栏（```）、**不得**包含任何解释性前言或 JSON 之外的文字。输出必须可被标准 JSON 解析器直接解析。

```json
{
  "trace_id": "string",
  "status": "correct|incorrect|undetermined",
  "primary_category": "7类之一或null",
  "findings": [
    {
      "stage": "六环节之一",
      "category": "7类之一",
      "locating": "具体定位（如第几步、哪句话）",
      "evidence": "判定依据（指向具体 trace 步骤 / 参考性质 / 测试用例）",
      "suggestion": "改进建议"
    }
  ],
  "confidence": null,
  "confidence_method": null,
  "calibration_version": null
}
```

语义约束：

- `correct`：`primary_category = null`，`findings = []`（所有适用环节正确）。
- `incorrect`：`primary_category` 非 null，`findings` 非空，且 `primary_category` 必须出现在某条 `findings[].category` 中。
- `undetermined`：`primary_category = null`，`findings = []`，且**仅**在输入损坏 / 截断 / 不可读或真实歧义导致无法判断候选主张时使用。

`confidence`、`confidence_method`、`calibration_version` 在 Phase 4 之前固定为 `null`，不得填写任何数值或字符串。

---

现在，请基于上方输入，只输出符合上述格式的 JSON 对象。

<!-- HY3_PROMPT_END -->
