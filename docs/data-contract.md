# 数据契约（schema_version = 0.3.0，taxonomy_version = 1.0.0）

> 语言无关的数据字段约定，便于未来由 C++17 以结构化方式读取（如 JSON / 自定义文本）。
> **本阶段不编写解析器**，仅约定字段。
> 关联文档：评估流程见 `architecture.md`；错误类别见 `error-taxonomy.md`；阶段记录见 `journal/phase-01a.md`。
> 当前版本：`schema_version = 0.3.0`，`taxonomy_version = 1.0.0`（schema_version 由 0.2.0 升到 0.3.0，taxonomy_version 维持 1.0.0）。

## 0. 批量文件结构与可复现元数据（meta）

### 0.1 一题一文件的批量结构

数据集采用**一题一文件**的布局，便于独立加载、人工审查与增量扩展：

```
data/
├── manifest.json              # 数据集级汇总（版本、计数、审查状态）
└── problems/
    ├── cf_160A.json           # 单题完整记录
    ├── cf_545D.json
    └── cf_1398B.json
```

每个 `problems/<id>.json` 文件顶层**至少**包含以下 8 个键（顺序无强制要求）：

| 键 | 类型 | 说明 |
| --- | --- | --- |
| `meta` | object | 该题目的可复现元数据（见 0.2） |
| `problem` | object | 题目记录（见第 1 节） |
| `reference_verdict` | object | 参考结论（见第 3 节） |
| `test_cases` | array<test_case> | 测试用例（见第 4 节） |
| `reasoning_traces` | array<reasoning_trace> | 候选推理轨迹（见第 2 节） |
| `candidate_solutions` | array<candidate_solution> | 可选候选 C++17 解法（见第 5 节） |
| `diagnoses` | array<diagnosis> | 诊断结果，每条轨迹一个（见第 6 节） |
| `verification_results` | array<verification_result> | 运行验证结果（见第 7 节），Phase 1A 为空数组 |

约束：
- `reasoning_traces`、`diagnoses` 必须一一对应：`diagnoses` 中每条记录的 `trace_id` 必须能解析到 `reasoning_traces` 中某条轨迹；每条轨迹**有且只有一个**对应 diagnosis。
- `candidate_solutions` 为可选；仅当某条轨迹需要附带候选解法时才填。
- **没有候选解法时，不得创建运行结果**（`verification_results` 必须为空）。
- **没有真实执行时**，`verification_results` 必须为空；若仅为文档示意展示结构，其内部 `verdict` 只能是 `not_run`，`actual_output` / `runtime_ms` 必须为 `null`，且不得填写虚构运行时间或虚构执行结果。

### 0.2 meta 字段

位于每个问题文件顶层 `meta`，确保实验可复现：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| schema_version | string | 数据契约版本，本阶段固定 `0.3.0` |
| taxonomy_version | string | 错误分类体系版本，固定 `1.0.0` |
| source_reference | string | 题目来源说明（题号 / 题名 / 出处，附官方链接） |
| dataset_version | string | 数据集版本或批次标识（本批次 `phase1a-pilot-001`） |
| created_at | string | 创建日期（ISO 8601，如 `2026-08-23`） |

> 推理轨迹的「生成与审查元数据」（`trace_origin`、`generator_model`、`annotator`、`review_status`、`reviewer`、`reviewed_at`）直接挂在每条 `reasoning_trace` 上（见第 2 节），**不得**把模型生成内容伪装成 `human_written` 或 `expert-reviewed`。

## 1. 题目记录（problem）

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| id | string | 题目唯一标识，如 `cf_1398B` |
| source | string | 来源，初版固定 `codeforces` |
| title | string | 题名 |
| statement | string | 题面（准确中文摘要，附官方链接，不整段复制完整题面） |
| constraints | object | 输入规模、数值范围等 |
| algorithm_type | string | 初版人工标注 `greedy` |
| reference_tags | array<string> | 参考标签（如 `sorting`, `greedy`, `game`） |
| notes | string | 可选备注 |

## 2. 候选推理轨迹（reasoning_trace）

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| id | string | 轨迹唯一标识 |
| problem_id | string | 关联题目 id |
| author | string | 参赛者 / 来源标识（本批次统一为 `hy3`） |
| trace_origin | string | 来源：`human_written` / `model_generated` / `transformed`。**Phase 1A 固定 `model_generated`** |
| generator_model | string | 生成模型。**Phase 1A 固定 `hy3`** |
| annotator | string | 标注者或标注来源。**Phase 1A 固定 `hy3_draft`** |
| review_status | string | 审查状态。`pending_planner_review`（模型生成、尚未复核）或 `planner_reviewed`（规划方已复核）。状态转换语义见 §2.1。 |
| reviewer | string \| null | 审查人；未审查为 `null` |
| reviewed_at | string \| null | 审查时间（ISO 8601）；未审查为 `null` |
| steps | array<step> | 推理环节，建议按六环节顺序（见 architecture 第 4 节） |
| intended_outcome | string | 可选：该题期望结论摘要 |

> 模型（Hy3）生成的轨迹**必须**保持 `trace_origin = model_generated`、`annotator = hy3_draft`。其 `review_status` 在生成时为 `pending_planner_review`（且 `reviewer`/`reviewed_at` 为 `null`）；规划方复核通过后方可转为 `planner_reviewed`（并填 `reviewer`/`reviewed_at`）。在任何阶段都**不得**把 `model_generated` 内容伪装成 `human_written` 或 `expert-reviewed`。

> **`implementation_consistency` 环节的前提（重要）**：该环节评估「文字思路与所附候选 C++17 代码是否一致」，**仅在轨迹确实关联 `candidate_solution` 时才可评估**。当某条轨迹**没有**关联 `candidate_solution` 时，**必须省略** `implementation_consistency` 步骤，**不得**出现“思路与代码一致”“思路与实现一致”等任何无代码支撑的断言。缺少可选候选代码本身**不会**使该纯思路轨迹自动变成 `incorrect` 或 `undetermined`——是否错误完全取决于其 `problem_understanding` / `greedy_choice` / `greedy_proof` / `complexity` / `boundary` 环节本身（见 `error-taxonomy.md`）。

### 2.1 审查状态语义（review_status）

- `model_generated` 样本在创建时，`review_status` 固定为 `pending_planner_review`（规划方尚未复核）。
- 后续规划方复核通过后，可转为 `planner_reviewed`；但 `planner_reviewed` **不等同于** `human_reviewed` 或 `expert-reviewed`，仅是「规划方已复核」的内部状态，不表示人工 / 专家背书。
- **当前 Phase 1A 基线已经是 `planner_reviewed`**：规划方（codex_planner）复核已于 2026-08-23 完成，全部 9 条轨迹 `reviewer = codex_planner`、`reviewed_at = 2026-08-23`；此状态**不等同于**人工（human_reviewed）或专家（expert-reviewed）审查背书。
- 在任何阶段都**不得**把 `model_generated` 内容伪装成 `human_written` 或 `expert-reviewed`。

`step` 结构：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| stage | string | 六环节之一（stage key，见 architecture 第 4 节） |
| text | string | 该环节的推理文本 |
| relies_on | array<string> | 依赖的前置环节（可空） |

## 3. 参考结论（reference_verdict）

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| problem_id | string | 关联题目 |
| expected_choice | string | 正确贪心策略描述 |
| expected_proof | string | 可审查的正确性论证（交换论证 / 性质） |
| expected_complexity | string | 期望时间 / 空间复杂度 |
| expected_boundaries | array<string> | 关键边界 |
| common_wrong_strategy_counterexample | string | 至少一个针对常见错误策略的反例说明（含对应 test_case id） |

> 判定参考结论时，**不要**只因为候选策略与参考文字不同就判错；必须给出逻辑证明或反例。

## 4. 测试信息（test_case）

描述一个测试用例，用于后续（Phase 2+）的验证与对照：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| id | string | 测试用例唯一标识 |
| problem_id | string | 关联题目 id |
| input | string | **可直接传给候选程序的完整标准输入**（含组数、各测试组等全部前置行） |
| expected_output | string | **对应的完整标准输出**（含各测试组结果等全部后置行） |
| origin | string | 来源：`official_sample` / `manually_designed` / `counterexample` / `generated` |
| purpose | string | 用途：`normal` / `boundary` / `complexity` / `counterexample` |
| notes | string | 可选备注；对 `counterexample` 必须说明它击破哪条 trace 及原因 |

> 只有真正来自官方题面的原始样例才能标记 `official_sample`（并记录官方 URL）；由我们基于公开题面自行构造的用例一律标记 `manually_designed` 或 `counterexample`，**不得**伪装成官方内容。本阶段仅定义字段与示例；不实现执行器，不连接外部 OJ。

## 5. 候选 C++17 解法（candidate_solution，可选）

当某条轨迹需要附带候选解法用于后续一致性验证时填写：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| id | string | 唯一 ID |
| trace_id | string | 关联推理轨迹 id |
| language | string | 语言，固定 `cpp` |
| standard | string | 语言标准，固定 `c++17` |
| source_code | string | 代码文本 |
| execution_status | string | `not_run` / `passed` / `failed` / `error` |

> Phase 1A **不执行**候选代码，因此本批次所有 `candidate_solution.execution_status` 必须为 `not_run`，且**不得**声称已编译或运行。仅当提供候选解法时才出现该数组项。某条轨迹未提供候选解法时，`candidate_solutions` 中不包含该轨迹的项，且对应的 `implementation_consistency` 步骤应省略（见第 2 节）。

## 6. 诊断结果（diagnosis）

顶层不再使用单一 `overall`，改为：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| id | string | 诊断唯一标识（建议 `<trace_id>_d`） |
| trace_id | string | 关联轨迹 id |
| status | string | 诊断状态：`correct` / `incorrect` / `undetermined` |
| primary_category | string \| null | 主要错误类别（7 类之一）；`correct` 或 `undetermined` 时为 `null` |
| findings | array<finding> | 一个或多个具体错误（可多填，不丢后续错误） |
| confidence | number \| null | 置信度 0..1；**Phase 4 校准前必须为 null** |
| confidence_method | string \| null | 置信度计算方法；未校准为 null |
| calibration_version | string \| null | 校准版本；未校准为 null |

`finding` 结构：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| stage | string | 出错 / 评估环节（stage key） |
| category | string | 错误类别（7 类之一，见 error-taxonomy） |
| locating | string | 定位描述（如「交换论证第 2 步」） |
| evidence | string | 判定依据 |
| suggestion | string | 改进建议 |

### primary_category 选取规则

1. 优先选择推理链中**最早出现且会使后续结论失效的根因**作为 `primary_category`。
2. 其余错误一律保留在 `findings` 中，**不允许只保留 primary_category 而丢弃后续错误**。
3. `status` 确定方式：
   - 无任何错误 → `correct`，`primary_category = null`；
   - 存在错误 → `incorrect`，`primary_category` 为根因；
   - 输入本身不足、无法可靠判断 → `undetermined`，`primary_category = null`（见 error-taxonomy 中 `undetermined` 与 `missing_greedy_proof` 的区别）。

> `confidence` 在 Phase 4 校准前保持 `null`，**不得用于正式比较或排序**。

## 7. 运行验证结果（verification_result，可选）

当（在后续阶段）实际运行候选解法后，记录对照结果；**Phase 1A 不填充**（所有问题文件该数组为空）：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| solution_id | string | 关联候选解法 id |
| test_id | string | 关联 test_case id |
| actual_output | string \| null | 实际输出（对应 `expected_output` 的完整标准输出）；未执行为 `null` |
| verdict | string | `not_run` / `pass` / `fail` / `compile_error` / `runtime_error` / `timeout` |
| runtime_ms | number \| null | 运行时间（毫秒）；未执行为 `null` |
| finding_ref | string \| null | 可选：关联的 `finding` 标识（把运行结果对应到具体错误） |

规则：
- 没有候选解法时，不得创建运行结果。
- 没有真实执行时，`verdict` 必须为 `not_run`，`actual_output` / `runtime_ms` 必须为 `null`。
- 不得填写虚构运行时间或虚构执行结果。
- 当前 Phase 1A 不执行代码，因此 `verification_results` 应为空数组；如需展示结构，只能在文档示意中使用 `not_run`。

## 8. 小型示例（仅示意字段，非真实样本）

以下片段用于说明「一题一文件 + 数组化」结构；其内容与真实评测样本无关，且 `verification_results` 为空（Phase 1A 不执行）：

```json
{
  "meta": {
    "schema_version": "0.3.0",
    "taxonomy_version": "1.0.0",
    "source_reference": "Codeforces 160A (Twins)",
    "dataset_version": "phase1a-pilot-001",
    "created_at": "2026-08-23"
  },
  "problem": {
    "id": "cf_160A",
    "source": "codeforces",
    "title": "Twins",
    "statement": "从 n 枚硬币中选子集，使所选面值之和严格大于剩余面值之和，且所选硬币数最少。题面与约束见官方链接。",
    "constraints": { "n_max": 100, "a_i_max": 100 },
    "algorithm_type": "greedy",
    "reference_tags": ["sorting", "greedy"]
  },
  "reference_verdict": {
    "problem_id": "cf_160A",
    "expected_choice": "硬币降序排序，依次取最大直到已取和严格大于剩余和。",
    "expected_proof": "（交换论证示例）每次取当前最大面值最优。",
    "expected_complexity": "O(n log n)",
    "expected_boundaries": ["n=1 直接取唯一硬币", "等值硬币需严格大于"],
    "common_wrong_strategy_counterexample": "误用 >=：两枚等值硬币时只取 1 枚即误判。"
  },
  "test_cases": [
    {
      "id": "cf_160A_c1",
      "problem_id": "cf_160A",
      "input": "3\n2 1 2\n",
      "expected_output": "2\n",
      "origin": "official_sample",
      "purpose": "normal",
      "notes": "官方样例（逐字）。"
    }
  ],
  "reasoning_traces": [
    {
      "id": "cf_160A_t1",
      "problem_id": "cf_160A",
      "author": "hy3",
      "trace_origin": "model_generated",
      "generator_model": "hy3",
      "annotator": "hy3_draft",
      "review_status": "pending_planner_review",
      "reviewer": null,
      "reviewed_at": null,
      "steps": [
        { "stage": "problem_understanding", "text": "（示例）", "relies_on": [] }
      ],
      "intended_outcome": "最少硬币数使已取和严格大于剩余和。"
    }
  ],
  "candidate_solutions": [],
  "diagnoses": [
    {
      "id": "cf_160A_t1_d",
      "trace_id": "cf_160A_t1",
      "status": "correct",
      "primary_category": null,
      "findings": [],
      "confidence": null,
      "confidence_method": null,
      "calibration_version": null
    }
  ],
  "verification_results": []
}
```

> 以上示例仅用于说明字段与数组化结构，**不**作为真实评测样本。

---

## 附录 A. Phase 1B 可执行校验规则与错误码映射

> **本附录为 Phase 1B 新增，仅记录「校验器如何实现本契约的检查」，不改变 0.3.0 的数据语义。**
> 校验器见 `hy3_algotrace validate <data_dir>`（C++17，实现见 `src/validator.cpp`、`docs/journal/phase-01b.md`）。
> 错误码稳定且机器可读，测试仅断言错误码而非自由文本。

### A.1 规则 → 错误码

| 契约约束（含第 0–7 节） | 违反时的稳定错误码 |
| --- | --- |
| 题文件缺 8 个必需顶层键之一 | `E_MISSING_KEY` |
| 字段类型错误（应为 string/int/array/object 等） | `E_TYPE_MISMATCH` |
| 字段校验三层顺序：① 存在性（缺键 → `E_MISSING_KEY`，不得静默当 null）② 声明类型（错类型 → `E_TYPE_MISMATCH`）③ 阶段语义（枚举/固定值/审查状态/未校准）。非 null 的 confidence 字段即使类型错误也仍同时报告 `E_TYPE_MISMATCH` 与 `E_UNCALIBRATED_CONFIDENCE` | `E_MISSING_KEY` / `E_TYPE_MISMATCH` / `E_UNCALIBRATED_CONFIDENCE` |
| `meta.schema/taxonomy/dataset_version` 与 manifest 不一致 | `E_VERSION_MISMATCH` |
| `problem.id` 与文件名 `<id>.json` 不匹配 | `E_PROBLEM_ID_FILE_MISMATCH` |
| `reference_verdict.problem_id` ≠ `problem.id` | `E_BAD_PROBLEM_FK` |
| `reasoning_traces[].id` 重复 | `E_DUPLICATE_ID` |
| `reasoning_traces[].problem_id` 无法解析到题目 | `E_BAD_PROBLEM_FK` |
| `reasoning_traces[].review_status` 非法枚举 | `E_INVALID_ENUM` |
| `review_status=planner_reviewed` 但 `reviewer`/`reviewed_at` 为空（或反之） | `E_REVIEW_STATUS_SEMANTIC` |
| `steps[].stage` 非法枚举 | `E_INVALID_ENUM` |
| 含 `implementation_consistency` 步骤但无关联 `candidate_solution` | `E_IMPLEMENTATION_WITHOUT_SOLUTION` |
| `candidate_solutions[].id` 重复 | `E_DUPLICATE_ID` |
| `candidate_solutions[].trace_id` 无法解析到轨迹 | `E_BAD_TRACE_FK` |
| `candidate_solutions[].language/standard/execution_status` 非法 | `E_INVALID_ENUM` |
| `test_cases[].id` 重复 | `E_DUPLICATE_ID` |
| `test_cases[].problem_id` 无法解析到题目 | `E_BAD_TEST_FK` |
| `test_cases[].origin` / `purpose` 非法枚举 | `E_INVALID_ENUM` |
| `verification_results` 非空（Phase 1A 不变量） | `E_UNEXPECTED_VERIFICATION_RESULT` |
| `verification_results[].solution_id` / `test_id` 无法解析 | `E_BAD_SOLUTION_FK` / `E_BAD_TEST_FK` |
| `verification_results[].verdict` 非法枚举 | `E_INVALID_ENUM` |
| `diagnoses[].trace_id` 无法解析到轨迹 | `E_BAD_TRACE_FK` |
| 同一轨迹 diagnosis 数量 ≠ 1 | `E_DIAGNOSIS_CARDINALITY` |
| `diagnoses[].status` 非法枚举 | `E_INVALID_ENUM` |
| `status=correct` 但 `primary_category` 非空或 `findings` 非空 | `E_CORRECT_WITH_FINDINGS` |
| `status=incorrect` 但 `findings` 为空 | `E_INCORRECT_WITHOUT_FINDINGS` |
| `status=incorrect` 但 `primary_category` 未出现于某 `finding.category` | `E_PRIMARY_NOT_IN_FINDINGS` |
| `status=undetermined` 但 `primary_category` 非空 | `E_STATUS_PRIMARY_MISMATCH` |
| `diagnoses[].primary_category` 缺键 / 非 string\|null / 非 7 类枚举（incorrect 时） | `E_MISSING_KEY` / `E_TYPE_MISMATCH` / `E_INVALID_ENUM` |
| `diagnoses[].confidence` 非 number\|null（Phase 4 前须为 null） | `E_TYPE_MISMATCH` / `E_UNCALIBRATED_CONFIDENCE` |
| `diagnoses[].confidence_method` 非 string\|null（Phase 4 前须为 null） | `E_TYPE_MISMATCH` / `E_UNCALIBRATED_CONFIDENCE` |
| `diagnoses[].calibration_version` 非 string\|null（Phase 4 前须为 null） | `E_TYPE_MISMATCH` / `E_UNCALIBRATED_CONFIDENCE` |
| `reasoning_traces[].reviewer` / `reviewed_at` 缺键或类型非 string\|null | `E_MISSING_KEY` / `E_TYPE_MISMATCH` |
| `problem.reference_tags` 元素非 string | `E_TYPE_MISMATCH` |
| `reference_verdict.expected_boundaries` 元素非 string | `E_TYPE_MISMATCH` |
| `reasoning_traces[].steps[].relies_on` 元素非 string | `E_TYPE_MISMATCH` |
| `manifest.problem_ids` 元素非 string | `E_TYPE_MISMATCH` |
| `manifest` 必填字段（schema_version/taxonomy_version/dataset_version/problem_count/trace_count/problem_ids/category_counts/status_counts/test_origin_counts/review_status/reviewer/reviewed_at）缺键或类型错 | `E_MISSING_KEY` / `E_TYPE_MISMATCH` |
| `manifest.review_status` 非法枚举 | `E_INVALID_ENUM` |
| `manifest.reviewer`/`reviewed_at` 与 `review_status` 语义不符（planner_reviewed 须非空，pending 须 null） | `E_REVIEW_STATUS_SEMANTIC` |
| `verification_results[]` 字段（solution_id/test_id/actual_output/verdict/runtime_ms 必填，finding_ref 可选 string\|null）缺键或类型错 | `E_MISSING_KEY` / `E_TYPE_MISMATCH` / `E_INVALID_ENUM` |
| `findings[].stage` / `category` 非法枚举 | `E_INVALID_ENUM` |
| `confidence` 在 Phase 4 校准前非 null | `E_UNCALIBRATED_CONFIDENCE` |
| manifest 重算计数（problem/trace/category/status/test_origin/ids）与汇总不一致 | `E_MANIFEST_COUNT_MISMATCH` |
| 数据集目录 / manifest / problems 目录缺失 | `E_DATA_DIR_NOT_FOUND` / `E_MANIFEST_NOT_FOUND` / `E_PROBLEMS_DIR_NOT_FOUND` |
| 文件无法打开 / JSON 解析失败 | `E_FILE_READ` / `E_JSON_PARSE` |
| CLI 参数错误 / 内部异常 | `E_USAGE` |

### A.2 错误码全集（稳定清单）

```
E_USAGE
E_DATA_DIR_NOT_FOUND
E_MANIFEST_NOT_FOUND
E_PROBLEMS_DIR_NOT_FOUND
E_FILE_READ
E_JSON_PARSE
E_MISSING_KEY
E_TYPE_MISMATCH
E_VERSION_MISMATCH
E_DUPLICATE_ID
E_BAD_PROBLEM_FK
E_BAD_TRACE_FK
E_BAD_SOLUTION_FK
E_BAD_TEST_FK
E_DIAGNOSIS_CARDINALITY
E_INVALID_ENUM
E_CORRECT_WITH_FINDINGS
E_INCORRECT_WITHOUT_FINDINGS
E_PRIMARY_NOT_IN_FINDINGS
E_IMPLEMENTATION_WITHOUT_SOLUTION
E_MANIFEST_COUNT_MISMATCH
E_UNCALIBRATED_CONFIDENCE
E_UNEXPECTED_VERIFICATION_RESULT
E_PROBLEM_ID_FILE_MISMATCH
E_REVIEW_STATUS_SEMANTIC
E_STATUS_PRIMARY_MISMATCH
```
