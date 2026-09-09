# 正式 Hy3 评测集合：冻结前作者确认单

正式实验 ID：`formal-hy3-greedy-v2-20260909`。当前状态 `pending_human_review`，**尚未冻结、尚未调用模型**。

## 为什么是 9 条而不是 12 条

12 条完整分组方案在当前保守算法下最坏新增预留约 288k，超过历史 76,496 token 后的 223,504 余额。固定 9 条后仍满足 basic/medium/hard 各 3 条和关键类别覆盖；每条输出上限 13,312，输入按实际 UTF-8 Prompt 字节数计上界，另加 1,024 API envelope 余量。预计算最坏新增约 216,016，最终数由冻结入口重新计算，不以此近似值放行。

TokenHub 的公开协议说明确认：`max_tokens` 是单次响应最大输出 token，达到时 `finish_reason=length`；`completion_tokens` 已包含 reasoning token，且 `total_tokens=prompt_tokens+completion_tokens`。来源：[TokenHub OpenAI Chat Completions 协议字段说明](https://cloud.tencent.com/document/product/1823/135872)。

## 固定分母候选

| 难度 | ID | 题目 | gold | 固定答案证据 | 选择理由 |
| --- | --- | --- | --- | --- | --- |
| basic | s004 | 等待安排 | correct | 3/3 passed | 正确过程对照 |
| basic | s005 | 等待安排 | wrong_greedy_choice, L8 | 2/3 passed | 错误贪心 |
| basic | s006 | 等待安排 | problem_misunderstanding, L9 | 0/3 passed | 目标理解错误 |
| medium | s010 | 合并账单 | correct | 3/3 passed | 正确过程对照 |
| medium | s011 | 合并账单 | wrong_greedy_choice, L7 | 1/3 passed | 错误贪心 |
| medium | s012 | 合并账单 | code_logic_error, L8 | 1/3 passed | 实现逻辑错误 |
| hard | s023 | 容量配对 | boundary_omission, L9 | 1/3 passed | 边界错误 |
| hard | s024 | 容量配对 | complexity_error, L8 | 3/3 passed | 答案测试通过但复杂度不成立 |
| hard | s037 | 离线缓存装载 | invalid_greedy_proof, 文字 | 4/4 passed | 答案测试通过但证明错误 |

开发题与正式题按题隔离；s001–s003 不进入正式指标。未选 holdout 保持附录，不会在看到输出后替换进分母。详细依据见[原始25条审核包](materials-original-25.md)和[新增12条审核包](materials-expansion-12.md)。

## 需要作者明确确认

请对上表 9 条给出 `gold_confirmed` 或逐条勘误，并确认同意将这 9 条作为不可事后更换的正式分母。未明确确认前，程序的正式门禁会拒绝 dry-run 和网络调用。
