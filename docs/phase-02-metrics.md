# Phase 2A — 评价指标定义（metrics）

> 配套文档：`docs/phase-02-protocol.md`（协议）、`prompts/hy3-evaluator-v1.md`（Prompt 模板）、`docs/journal/phase-02a.md`（设计记录）。
> 评估协议版本：`evaluation_schema_version = 0.1.0`
> 适用范围：初版 9 条贪心题轨迹的 `reference_assisted` 冒烟实验（详见第 12 节规模与统计限制）。

本文件精确定义自动指标与人工定性指标，写清分母、去重方法与异常处理，确保 Phase 2C 运行时可复现计算。

---

## 0. 通用约定

- **轨迹集合**：记 `T` 为本次 run 的全部轨迹，总数 `N = |T|`（初版 `N = 9`）。**Phase 2A 尚未创建任何 run，故所有指标在 Phase 2A 为 `N/A` / `not_computed`。**
- **gold diagnosis**：来自 `data/problems/<id>.json` 的 `diagnoses[]`，是评价目标；**不参与**任何输入渲染，只在 Reporter 阶段内存比较。
- **prediction**：来自 `predictions/<trace_id>.json` 的 `prediction` 字段；`parse_status != parsed` 时记为「解析失败」。
- **解析失败的统一处理**（关键，修正版）：对 `parse_status ∈ {model_call_not_attempted, empty_response, invalid_json, schema_invalid, semantic_invalid}` 的轨迹：
  - `status_accuracy`：计 0（不一致）。
  - `primary_category_accuracy`：若该轨迹在指标分母（`gold.status == incorrect`）中，计 0。
  - `finding_category_macro_F1`：该轨迹固定为 **0**（**不得**因为 prediction 不存在而套用「空集合对空集合 = 1」）。
  - `finding_category_micro_*` / `stage_category_pair_micro_*`：使用内部 sentinel（见下），不参与「空对空 = 1」优惠。
- **内部 sentinel（仅指标计算，绝不写回 prediction JSON）**：
  - micro finding 类别：对解析失败轨迹，预测集合视为含单个 sentinel 元素 `__parse_failed__`。
    - gold 为 `incorrect`：同时产生 sentinel 的 FP 与遗漏 gold 各类别的 FN。
    - gold 为 `correct` / `undetermined`：产生 sentinel 的 FP。
  - micro stage-category 对：同理，预测集合视为含单个 sentinel 对 `(__parse_failed__, __parse_failed__)`。
- **「空集合对空集合 = 1」的适用条件**（严格限制）：仅当①`parse_status == parsed`；②prediction findings 确实为空；③gold findings 也为空。解析失败一律不适用。
- **去重**：涉及 finding 类别集合时，先将单条轨迹的 `findings[].category` 去重为集合（同一类别出现多次只算一次）。
- **`confidence`**：Phase 4 之前固定为 `null`，**不纳入任何指标**。

---

## 1. parse_success_rate（解析成功率）

- **公式**：`count(parse_status == "parsed") / N`
- **分母**：`N`（总轨迹数，含解析失败）。
- **说明**：衡量 prompt 模板与输出契约是否能被 Hy3 稳定遵循。**Phase 2A 尚未创建 run，无轨迹可计，该指标为 `N/A` / `not_computed`；不得报告为 0。** 只有未来创建 run 并将轨迹加入后，未调用模型的轨迹才记为 `model_call_not_attempted`，此时该值才可能被计算（初版若 9 条均未调用则为 0，但那是 run 已存在时的结果，不是 Phase 2A 阶段状态）。

---

## 2. status_accuracy（状态一致率）

- **公式**：`count(predicted_status == gold_status) / N`
- **分母**：`N`（总轨迹数）。
- **解析失败处理**：预测非 `parsed` 时视为预测失败，按「不一致」计（即该轨迹不计入分子）。
- **说明**：评估整体 correct / incorrect / undetermined 判定是否与 gold 一致。

---

## 3. primary_category_accuracy（主错误类别准确率）

- **分母固定为**：`gold.status == "incorrect"` 的轨迹集合 `I`（即只看确实有错误的轨迹）。
- **公式**：`count(预测正确) / |I|`
- **计错条件**（满足任一即不计分子）：
  - 预测 `status != incorrect`（如判为 correct 或 undetermined）；
  - 预测 `primary_category == null`；
  - 预测 `primary_category` 与 gold `primary_category` 不同。
- **说明**：只对「确实错误」的轨迹评估主因定位；correct / undetermined 轨迹不进入此指标分母。

---

## 4. finding_category_micro_precision / recall / F1

- **逐条处理**：对每条轨迹 `t`，将 `findings[].category` 去重为集合 `P_t`（预测）与 `G_t`（gold，仅取 gold 中 `status == incorrect` 的轨迹；gold 为 correct/undetermined 时 `G_t = ∅`）。
- **汇总**：
  - `TP = Σ_t |P_t ∩ G_t|`
  - `FP = Σ_t |P_t \ G_t|`
  - `FN = Σ_t |G_t \ P_t|`
- **指标**（零分母规则，不使用单独的 FP=0 / FN=0 判断）：
  - `micro_precision = TP / (TP + FP)`；当 `TP + FP = 0` 时，precision 记为 `1`。
  - `micro_recall = TP / (TP + FN)`；当 `TP + FN = 0` 时，recall 记为 `1`。
  - `micro_F1` 由上述 precision（`P`）与 recall（`R`）按 `2·P·R / (P + R)` 计算；当 `P + R = 0` 时记 `0`。
  - 说明：零分母统一以「分子+分母之和为 0」判定（即 `TP+FP=0` 与 `TP+FN=0`），而非单独检查 `FP=0` 或 `FN=0`。
- **说明**：micro 视角下**每个去重后的 trace-category 项同等加权**（不是「每条 finding 同等加权」；同轨迹重复类别已去重，不重复计数）。gold 为 correct 的轨迹其 `G_t = ∅`，若预测也空（`P_t = ∅`）则不贡献 TP/FP/FN；若预测非空则贡献 FP。解析失败轨迹按第 0 节 sentinel 处理。

---

## 5. finding_category_macro_F1（类别集合 macro F1）

- **逐条轨迹 F1**：对每条轨迹 `t`，先算其类别集合 F1：
  - 若 `parse_status != parsed`（解析失败）：**固定记 0**（见第 0 节，不得套用「空对空 = 1」）。
  - 否则若 `P_t = ∅` 且 `G_t = ∅`（parsed 且 prediction 与 gold 均无 finding）：**记 1**（空集合对空集合视为完全匹配）。
  - 否则按集合计算 `F1_t = 2·|P_t∩G_t| / (|P_t| + |G_t|)`（分母为 0 不可能，因已排除双空）。
- **汇总**：`macro_F1 = mean(F1_t over t ∈ T)`
- **说明**：macro 视角下每条轨迹同等加权，避免多条 finding 密集的轨迹主导指标；「空对空 = 1」**仅限** `parse_status == parsed` 且 prediction/gold 均无 finding（见第 0 节适用条件）。

---

## 6. stage_category_pair_micro_F1（环节-类别配对 F1）

- **映射**：将每条 finding 映射为有序对 `(stage, category)`；对每条轨迹去重为该对的集合 `SP_t`（预测）与 `SG_t`（gold）。
- **汇总**（同第 4 节 micro 方法，但作用在 `(stage, category)` 对上）：
  - `TP = Σ_t |SP_t ∩ SG_t|`
  - `FP = Σ_t |SP_t \ SG_t|`
  - `FN = Σ_t |SG_t \ SP_t|`
  - `micro_precision / recall / F1` 使用与第 4 节**相同的零分母规则**（`TP+FP=0` 时 precision 记 1；`TP+FN=0` 时 recall 记 1）。
- **说明**：比单纯类别更严格——即使类别判对，若定位到错误环节（stage），仍计 FN/FP。用于评估「定位精度」。**微观加权同样基于去重后的 trace-(stage,category) 项**；解析失败轨迹按第 0 节 sentinel 对处理。

---

## 7. status confusion matrix（状态混淆矩阵）

- **维度**：行 = gold status，列 = predicted status；类别为 `{correct, incorrect, undetermined}`。
- **解析失败处理**：预测非 `parsed` 时，预测状态记为特殊行/列 `parse_failed`（或在矩阵外单列），不计入 `{correct, incorrect, undetermined}` 的 3×3 主体但需在报告中说明数量。
- **说明**：观察错误模式（如是否把 incorrect 误判为 correct，或把 correct 误判为 undetermined）。

---

## 8. primary_category confusion matrix（主错误类别混淆矩阵）

- **分母**：仅 `gold.status == incorrect` 的轨迹 `I`。
- **维度**：行 = gold primary_category（7 类之一），列 = predicted primary_category（7 类之一，或 `null` / `parse_failed` 作为额外列）。
- **说明**：观察 7 类错误之间的混淆模式（如 `missing_greedy_proof` 与 `invalid_greedy_proof` 是否互混）。

---

## 9. undetermined_rate（待定率）

- **公式**：`count(predicted_status == "undetermined") / N`
- **说明**：监控 Hy3 是否过度使用 `undetermined`。结合第 3 节协议约束——「缺少贪心证明」不得误标为 undetermined——该指标异常升高可能提示类别误用。

---

## 10. hallucination_flag_rate（幻觉标注率）

- **定义**：prediction 的 `evidence` 引用了**不存在**的内容（不存在的 trace 步骤编号、不存在的测试用例 id、或声称运行/编译/提交了候选代码）的轨迹占比。
- **公式**：`count(含幻觉 evidence 的轨迹) / N`
- **判定方式**：人工或 Phase 2C 的辅助检查（核对 evidence 引用的步骤 / 测试是否在输入中真实存在；核查是否出现「我运行了代码」类表述）。
- **说明**：本指标依赖人工核查或后续规则检查器，不在纯自动指标内强制计算；记录为定性/半定量信号。

---

## 11. 人工定性审查（不自动计分）

以下三项由人工评审员对每条轨迹的 prediction 打分（0 / 1 / 2），**不纳入自动指标汇总**，仅作定性报告：

| 维度 | 0 | 1 | 2 |
| --- | --- | --- | --- |
| `evidence_quality`（依据质量） | 无依据 / 依据错误 | 依据相关但不充分 | 依据具体、可验证、指向明确 |
| `suggestion_quality`（建议质量） | 无建议 / 建议错误 | 建议笼统 | 建议具体可操作 |
| `root_cause_quality`（根因质量） | 根因错 / 未定位 | 根因部分正确 | 根因准确且为最早失效点 |

- 仅在 `status == incorrect` 且 `findings` 非空时评估（correct / undetermined 不评或不计入均值）。
- 评分者、评分日期、评分标准版本应记入 `report.md` 的元信息。

---

## 12. 规模与统计限制（重要）

- **样本量**：初版仅 **9 条**贪心题轨迹，属**冒烟实验（smoke test）**，用于验证协议与 Prompt 模板是否可运行、可解析、可比较。
- **Phase 2A 无 run**：本阶段**尚未创建任何实验 run**，因此全部指标（含 `parse_success_rate`）均为 `N/A` / `not_computed`；**不得**把「尚未进行实验」报告为 `parse_success_rate = 0` 或声称「9 条轨迹 parse_status = model_call_not_attempted」。只有创建 run 并把轨迹加入后，未调用模型的轨迹才记录 `model_call_not_attempted`。
- **不做统计显著性结论**：9 样本任何率值置信区间极宽，不得声称「Hy3 的准确率为 X%」代表模型总体能力。
- **不得外推**：结果仅反映这 9 条样本在这套 prompt / 参考数据下的表现，**不宣称代表 Hy3 的总体推理审查能力**。
- **自动指标不评价自然语言优美度**：`evidence` / `suggestion` 的措辞质量由第 11 节人工审查覆盖，自动指标只看结构正确性。
- **confidence 不参与**：Phase 4 前 `confidence = null`，所有涉及置信度的校准指标暂不计算。

---

## 13. 指标计算输入边界（与协议对齐）

- 所有指标输入来自 `predictions/<trace_id>.json` 与 `data/problems/<id>.json` 的 gold diagnosis；二者在 Reporter 阶段关联，gold **不进入** prediction 文件（见协议第 7 节）。
- `parse_status` 决定 prediction 是否有效（协议第 5 节）；无效预测的计数规则见第 0 节。
- `evaluation_schema_version`（0.1.0）与数据 `schema_version`（0.3.0）独立，指标计算只依赖前者定义的 wrapper 结构。
