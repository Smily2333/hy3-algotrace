# 错误分类体系（v1，taxonomy_version = 1.0.0）

> 本体系用于 `diagnosis.findings[].category`。
> 注意：诊断还有两个**状态** `correct` 与 `undetermined`，它们**不是** finding 类别，见文末「状态说明」。
> 评估时按 `architecture.md` 第 4 节的六环节顺序逐项判定。
> 关联文档：数据字段见 `data-contract.md`；阶段划分见 `roadmap.md`。

## 类别总览（finding 类别，共 7 类）

| key | 中文 | 性质 |
| --- | --- | --- |
| `problem_misunderstanding` | 题意理解偏差 | 错误 |
| `wrong_greedy_choice` | 贪心策略选择错误 | 错误 |
| `missing_greedy_proof` | 缺失贪心证明 | 错误 |
| `invalid_greedy_proof` | 贪心论证错误 / 不成立 | 错误 |
| `complexity_error` | 复杂度分析错误 | 错误 |
| `boundary_omission` | 边界条件遗漏 | 错误 |
| `implementation_mismatch` | 实现与思路不一致 | 错误 |

> `correct`（无错误）与 `undetermined`（输入不足无法判断）是诊断**状态**，不填入 `findings[].category`。

---

## 1. problem_misunderstanding（题意理解偏差）

- **定义**：对题面、约束、目标或输入输出格式的误解。
- **判定信号**：错误陈述题意；忽略关键约束；目标函数理解反（如求最小却按最大）；样例解释错误。
- **易混淆 → `wrong_greedy_choice`**：前者错在「题目要什么」，后者错在「怎么选策略」。若误解题但策略逻辑自洽，归本类；若题意理解正确但选错策略，归 `wrong_greedy_choice`。

## 2. wrong_greedy_choice（贪心策略选择错误）

- **定义**：题目确适合贪心，但所选贪心量 / 排序 / 优先级错误，导致策略本身不成立。
- **判定信号**：可构造反例推翻；所选贪心量缺乏最优子结构依据；与参考策略明显不同且可证伪。
- **易混淆 → `problem_misunderstanding`**：若选错源于误解题，优先归 `problem_misunderstanding`。
- **易混淆 → `invalid_greedy_proof`**：若策略描述与参考一致但论证错误，归 `invalid_greedy_proof`；若策略本身不同，归本类。

## 3. missing_greedy_proof（缺失贪心证明）

- **定义**：任务**明确要求**给出正确性论证（性质或交换论证），但轨迹没有提供任何论证。
- **判定信号**：断言「显然正确」；直接给出策略无论证；仅举例未证明。
- **易混淆 → `invalid_greedy_proof`**：本类是完全没有论证；后者有论证但论证错误 / 不充分。
- **易混淆 → `undetermined`**：本类属**已确认错误**（我们知道要求证明，轨迹却没给）；若因信息不足连「是否要求证明」都无法判断，则归 `undetermined`。详见文末状态说明。

## 4. invalid_greedy_proof（贪心论证错误 / 不成立）

- **定义**：提供了交换论证或贪心性质，但论证逻辑错误、循环论证或不具一般性。
- **判定信号**：交换论证假设了待证结论；反例推翻论证；仅对特例成立。
- **易混淆 → `missing_greedy_proof`**：本类「有论证但错」；后者「无论证」。
- **易混淆 → `wrong_greedy_choice`**：若策略本身错，归 `wrong_greedy_choice`；若策略可能正确仅证明过程有误，归本类。

## 5. complexity_error（复杂度分析错误）

- **定义**：对时间 / 空间复杂度的估计或推导错误。
- **判定信号**：复杂度数量级错误；忽略排序 / 数据结构开销；与实现实际不符。
- **易混淆 → `implementation_mismatch`**：本类聚焦「分析值」错误；若分析对但代码实现实际更慢，归 `implementation_mismatch`。

## 6. boundary_omission（边界条件遗漏）

- **定义**：未考虑特殊输入或极端约束（空、单元素、最大范围、相等元素、取整边界、溢出等）。
- **判定信号**：未讨论 n=1、全相等、溢出、取整边界；代码无对应处理。
- **易混淆 → `implementation_mismatch`**：本类是「没想到边界」；若想到但实现写错，归 `implementation_mismatch`。

## 7. implementation_mismatch（实现与思路不一致）

- **定义**：给出的 C++17 解法与所陈述思路不一致（思路对、代码错，或反之）。
- **判定信号**：代码逻辑偏离文字描述；变量语义错位；思路声称处理某情况但代码未体现。
- **易混淆 → `boundary_omission` / `complexity_error`**：本类是「思路与代码对不上」；若纯属漏想边界归 `boundary_omission`，若纯属分析值错归 `complexity_error`。

---

## 状态说明：correct 与 undetermined

### correct（状态，非负类）

- 六环节均成立：贪心策略、论证、复杂度、边界、实现一致且无错误。
- 在 `diagnosis` 中表示为：`status = "correct"`，`primary_category = null`，`findings = []`。

### undetermined（状态，非已确认错误）

- **定义**：输入本身不足，无法可靠判断策略或证明是否正确。
- **映射**：原 `insufficient_evidence` 类别已移除，统一映射为 `status = "undetermined"`，`primary_category = null`。
- **判定信号**：轨迹过短；关键步骤缺失；自相矛盾无法归类；缺少题面 / 约束等必要信息。

#### undetermined 与 missing_greedy_proof 的区别（重要）

- `missing_greedy_proof`：**任务明确要求给出证明**，但轨迹没有提供任何论证。此时信息其实是足够的（我们知道要求是什么），只是轨迹缺失了该项 → 属于**已确认的推理错误**，填入 `findings`。
- `undetermined`：连「能否判断」都做不到，因为**输入本身不足**（如轨迹太短、关键步骤缺失、或题目信息不全），无法可靠下结论 → 属于**状态**，不是 finding 类别。
- 简记：前者「知道要证明却没给」= 错误；后者「给的信息不够，无从判断」= 待定。

> 设计约束：不得把 `undetermined` 当作已确认错误来计数或参与错误率统计。
