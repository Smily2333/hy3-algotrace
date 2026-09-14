# 正式 9 条结果人工复核包

实验 `formal-hy3-greedy-v2-20260909` 的分母在调用前固定为 9 条。9 条均 HTTP 200、各调用一次、无重试；只有 s005、s010 通过完整响应契约。其余结果仍计入分母，不从正式指标中删除。

## 契约结果

| 样本 | finish reason | parse status | 正式计分状态 | 原始响应 SHA-256 |
| --- | --- | --- | --- | --- |
| s004 | stop | schema_invalid | failed | `ef4a7f46bf62bfdf8b599c2bb56fc61d14f8ebbb19101869024bd60b63105569` |
| s005 | stop | parsed | incorrect / wrong_greedy_choice | `a5d3088c30223d948499dc8ddb768dd05d97d19bc737c232eb722a4fea5ec8b5` |
| s006 | length | empty_response | failed | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| s010 | stop | parsed | correct / none | `b6fd82ab387c02e59f6d96fa30d04ba66c99d74b32450f01a38d7f7b8317b28c` |
| s011 | stop | schema_invalid | failed | `053bd8cd6d823956528a2efbc413dcc7781901f21a46f10210532cd9d9b748b4` |
| s012 | stop | schema_invalid | failed | `7a4c46bc124018dc4aa9f825e8e2a5eaa49ce3729ff84af34493c70736efe6bf` |
| s023 | stop | schema_invalid | failed | `6584c04ba8ea909851edeb6b76fde737d97336cab89a923abd17ea9457cad38f` |
| s024 | length | invalid_json | failed | `c860264d5c9e30c85f108c09cc8441188c5695ed150b0439f421e14ce7a01417` |
| s037 | length | empty_response | failed | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |

## 已完成人工判断的两条可解析结果

### s005：等待安排

- Gold：`incorrect / wrong_greedy_choice`，首次错误为候选代码第 8 行降序排序。
- Hy3：同样判断 `incorrect / wrong_greedy_choice`，定位第 8 行 `sort(a.rbegin(),a.rend());`，建议改为升序。
- Hy3 反例：输入 `2 / 1 10`；静态推断候选输出 10，期望 1。
- Hy3 参考代码：按时长升序，累加每个任务开始前的等待时间；静态审查未发现危险能力，无网络隔离固定测试3/3通过。
- Smily2333 于2026-09-09确认：诊断、定位、反例及参考策略/证明/复杂度/边界全部成立。

### s010：合并账单

- Gold：`correct / none`。
- Hy3：判断 `correct / none`；识别为每次合并最轻两堆的小顶堆/Huffman 贪心。
- Hy3 参考代码：使用 `priority_queue<..., greater<...>>` 重复合并最小两堆；静态审查未发现危险能力，无网络隔离固定测试3/3通过。
- Smily2333 于2026-09-09确认：未告警合理，参考策略/证明/复杂度/边界全部成立。

完整结构化响应见 [`evaluation/results/formal-20260909-records.json`](../../evaluation/results/formal-20260909-records.json)。这里的人工复核只评价可解析内容；7 条契约失败不能凭原文中可能出现的零散语义改计为成功。

## 审核状态

本包已完成，不存在以默认同意或Planner自签代替真人审核的条目。7条契约失败保持失败；2条有效输出均已由项目作者审核。
