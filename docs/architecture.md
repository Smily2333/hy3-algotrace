# 系统架构（规划稿）

> 本文描述**未来**系统的模块与流程。Phase 0 仅冻结设计，不实现任何模块。
> 相关约定：数据字段见 `data-contract.md`；错误类别见 `error-taxonomy.md`；阶段划分见 `roadmap.md`。

## 1. 总体职责划分

系统按三条相互独立的职责线组织，避免把「判断算法类型」「评估推理过程」「验证代码 / 测试」混在同一处：

| 职责 | 名称 | 输入 | 输出 | Phase 0 状态 |
| --- | --- | --- | --- | --- |
| 算法类型识别 | `algorithm_type_identification` | 题面 + 约束 | 类型标签（greedy / search / dp / graph …） | 初版人工标注 `greedy` |
| 过程评估 | `process_evaluation` | 推理轨迹 + 题目 | 诊断（status / primary_category / findings） | 设计冻结 |
| 代码 / 测试验证 | `code_test_verification` | C++17 解法 + 测试信息 | 一致性 / 正确性信号（test_case, verification_result） | 仅规划 |

> 初版（Phase 0–3）所有样本人工标注为 `greedy`，由 `process_evaluation` 直接处理；自动路由在 **Phase 6** 引入。

## 2. 主要模块

- **Ingest（接入）**：载入题目记录与推理轨迹（格式见 `data-contract.md`）。本阶段不实现解析器。
- **TypeRouter（类型路由）**：初版为人工标注透传；Phase 6 起自动识别算法类型并分发。
- **ProcessEvaluator（过程评估器）**：核心。对每条推理轨迹，按六环节逐步评估，产出诊断（status / primary_category / findings，结构见 `data-contract.md`）。
- **CodeVerifier（代码验证器）**：将候选 C++17 解法与思路比对（实现一致性），并基于 `test_case` / `verification_result`（见 `data-contract.md`）评估正确性信号。Phase 2+ 落地。
- **Scorer（评分校准）**：将过程评估结果映射为分数 / 置信度，Phase 4 校准（校准前 `confidence` 为 null）。
- **Reporter（报告）**：汇总为诊断报告（见 README 输出形式）。

## 3. 处理流程

```
题目记录 ─┐
          ├─► Ingest ─► TypeRouter ─(greedy)─► ProcessEvaluator ─┐
候选推理轨迹 ─┘                                            │                   │
                                                            │                   ▼
候选 C++17 解法 ─────────────────────────────► CodeVerifier ┘            Scorer ─► Reporter ─► 诊断报告
```

文字描述：

1. `Ingest` 载入题目、推理轨迹与（可选）候选解法、测试信息。
2. `TypeRouter` 确定算法类型；初版直接采用人工 `greedy` 标签。
3. `ProcessEvaluator` 按六环节评估推理轨迹，调用错误分类体系（见 error-taxonomy）给出定位，并产出诊断（status / primary_category / findings）。
4. 若提供候选解法与测试信息，`CodeVerifier` 评估「实现与思路一致性」及运行验证结果，补充正确性信号。
5. `Scorer` 汇总（Phase 4 前 `confidence` 为 null），`Reporter` 输出诊断报告。

## 4. 六环节评估维度（固定顺序）

过程评估固定按以下顺序拆解（与错误分类一一对应）：

| 顺序 | stage key | 中文 | 说明 |
| --- | --- | --- | --- |
| 1 | `problem_understanding` | 题意理解 | 是否准确理解题面、约束与目标 |
| 2 | `greedy_choice` | 贪心策略选择 | 所选贪心量 / 排序 / 优先级是否正确 |
| 3 | `greedy_proof` | 贪心性质 / 交换论证 | 正确性论证是否成立 |
| 4 | `complexity` | 复杂度分析 | 时间 / 空间复杂度是否正确 |
| 5 | `boundary` | 边界条件 | 特殊 / 极端输入是否考虑 |
| 6 | `implementation_consistency` | 实现与思路一致性 | 代码与文字思路是否一致 |

## 5. 设计约束

- 类型识别、过程评估、代码验证三者解耦，可独立演进。
- 初版不实现自动路由、不调用模型 API、不连接外部 OJ。
- 所有模块接口在 Phase 1–2 依据 `data-contract.md` 落地。
- 诊断结论区分 `status`（correct / incorrect / undetermined）与 finding 类别（见 error-taxonomy）；`undetermined` 表示输入不足无法判定，**不作为已确认错误**参与计数。
- `implementation_consistency` 环节仅在轨迹关联候选 C++17 解法时才评估；无候选解法时该环节应**省略**，不得凭空断言「思路与实现一致」（详见 `data-contract.md` 第 2 节）。缺少可选代码**不会**使纯思路轨迹自动判错。
- 数据集采用**一题一文件**布局：`data/problems/<id>.json` 顶层含 `meta / problem / reference_verdict / test_cases[] / reasoning_traces[] / candidate_solutions[] / diagnoses[] / verification_results[]`，由 `data/manifest.json` 汇总（详见 `data-contract.md`）。Phase 1A 尚未执行候选代码，所有 `verification_results` 为空数组。
