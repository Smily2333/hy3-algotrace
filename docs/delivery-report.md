# hy3-algotrace 最终交付与正式实验报告

日期：2026-09-09。项目对应《犀牛鸟开源-实战任务-混元大语言模型项目》任务 2。本报告只陈述本仓库留下证据的结果。

## 1. 场景与目标

项目面向正在学习贪心算法和 C++17 的用户。用户粘贴完整题面与代码，Hy3 静态分析代码采用的算法、是否存在明确错误、首次错误步骤和代码位置，并提供证据、反例候选与完整修正解法。传统编译器或样例测试难以解释“答案碰巧正确但推理不成立”；引入 Hy3 的价值是生成可读的过程诊断，而非替代执行验证或形式化证明。

应用保持 C++17 后端、CMake 和原生网页，只绑定 loopback。网页不执行用户代码、不连接 OJ，Key 仅由服务端读取。

## 2. 系统与评估流程

```text
题面 + C++代码 → 交互v2请求校验 → Hy3单次静态分析
     → 严格JSON/schema/语义/行号校验 → 浏览器安全文本展示

冻结材料 → 无gold泄漏的评测Prompt → 单次正式调用 → 脱敏记录
     → 已批准代码的无网络隔离固定测试 → 真人过程复核 → 指标
```

交互 v2 的三态为“未发现明确错误 / 发现错误 / 无法确定”，不显示未经执行得到的 AC/WA/CE/RE/TLE。行号按 LF 规范化后的 1-based 范围核对，片段必须来自指定位置；这只验证定位与输入一致，不证明诊断正确。

正式评测使用独立 `greedy-evaluation-v2`。Prompt 不含 gold、难度、集合标签、标准答案或错误位置。解析器不补字段、不移动层级、不修复 JSON、不剥 Markdown fence。所有失败保留在固定分母。

## 3. 材料、来源与冻结

- 原材料：8题25候选；扩充材料：4题12候选；合计12题37候选。
- 题目为原创表述的受控贪心任务，并记录公开教学思想来源；未复制完整 OJ 题面。
- 每题有约束、参考实现、固定输入输出和独立 oracle 核对。37条 gold 均由 Smily2333 于2026-09-09确认。
- 正式集合在模型输出前固定为9条：`s004,s005,s006,s010,s011,s012,s023,s024,s037`，基础/中等/困难各3条。
- 覆盖正确过程、题意误解、错误贪心、实现逻辑、边界、复杂度和错误证明；s024、s037 是固定测试通过但过程不成立的对照。
- 开发样本和同题变体不进入正式分母，不按模型输出换样本。

冻结清单 SHA-256：`7b3d7a65ea791c7fdbb8b8326d52ade8c1111a123e27ab085618504152b5fafe`。正式数据 canonical SHA-256：`a70c6e9d1ca1dd7a87ff6629fa9360f05e49f9709221bb00e80c5198d2482dd3`。Prompt SHA-256：`a9041f59c05e547030d846cf9b4c4608e9c9d6b851aa4d44ccf1b06383c80b4a`。

## 4. 正式实验设置与调用结果

模型为 `hy3`，model version 未由服务返回，温度使用 provider default，`response_format=json_object`，输出上限13,312 token。9条均 HTTP 200、各一次、无重试。

| 样本 | 难度 | parse status | finish | prompt | completion | total |
| --- | --- | --- | --- | ---: | ---: | ---: |
| s004 | 基础 | schema_invalid | stop | 2,286 | 8,910 | 11,196 |
| s005 | 基础 | parsed | stop | 2,288 | 12,452 | 14,740 |
| s006 | 基础 | empty_response | length | 2,287 | 13,312 | 15,599 |
| s010 | 中等 | parsed | stop | 2,316 | 9,857 | 12,173 |
| s011 | 中等 | schema_invalid | stop | 2,316 | 12,854 | 15,170 |
| s012 | 中等 | schema_invalid | stop | 2,315 | 10,353 | 12,668 |
| s023 | 困难 | schema_invalid | stop | 2,322 | 12,400 | 14,722 |
| s024 | 困难 | invalid_json | length | 2,359 | 13,311 | 15,670 |
| s037 | 困难 | empty_response | length | 2,440 | 13,312 | 15,752 |

正式批次消耗127,690 token。连同6次开发调用76,496 token，全项目账本为15次、204,186 token，未知占用0，300,000上限内剩余95,814；没有为了耗尽额度继续调用。

## 5. 正式指标

| 指标 | 分子 / 分母 | 值 |
| --- | ---: | ---: |
| 正式输出契约通过率 | 2 / 9 | 0.2222 |
| 候选诊断一致率 | 2 / 9 | 0.2222 |
| Hy3完整解答最终答案准确率 | 2 / 9 | 0.2222 |
| 答案验证覆盖率 | 2 / 9 | 0.2222 |
| Hy3完整解答过程正确率 | 2 / 9 | 0.2222 |
| 人工过程审查覆盖率 | 2 / 9 | 0.2222 |
| 首次错误定位准确率 | 1 / 5 | 0.2000 |
| 过程正确样本误报率 | 0 / 2 | 0.0000 |
| 答案正确告警：真实问题/误报/未决 | 0 / 0 | N/A |

条件指标：两条契约有效响应的诊断一致率、参考代码固定测试通过率和人工过程正确率均为2/2（1.0000）。该条件结果不能替代正式9条总体指标。

失败构成：4个 `schema_invalid`、1个 `invalid_json`、2个 `empty_response`，传输/鉴权失败0；7条缺少可验证的结构化 `solution_code`。`undetermined` 为0，但契约失败不解释为模型“确定”。Gold类别分布为 none 2、wrong_greedy_choice 2，其余五类各1；有效预测仅 none 1、wrong_greedy_choice 1。

### 难度分层

| 难度 | n | 契约通过 | 诊断一致 | 首错定位 | 正确过程误报 | 解答答案/过程正确 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 基础 | 3 | 1/3 | 1/3 | 1/2 | 0/1 | 1/3 |
| 中等 | 3 | 1/3 | 1/3 | 0/2 | 0/1 | 1/3 |
| 困难 | 3 | 0/3 | 0/3 | 0/1 | N/A | 0/3 |

困难层3条全部契约失败，但样本太小，且失败受输出长度和契约遵循混杂，不能断言存在稳定的能力临界点。

## 6. 完整解答验证与真人审核

s005、s010 是仅有的两条合法完整响应。Planner 先确认代码只使用 STL 与 stdin/stdout；随后在 WSL bubblewrap 中以无网络、清空环境、受限资源方式编译运行。两份均编译成功，三个冻结测试全部通过。有限测试不构成完整正确性证明。

Smily2333 于2026-09-09审核：

- s005 的 wrong_greedy_choice 诊断、第8行定位、反例、升序策略、证明、复杂度和边界全部成立。
- s010 的未告警判断、Huffman/小顶堆策略、证明、复杂度和边界全部成立。
- 其余7条保持契约失败，不从零散原文中补救为成功。

材料审核覆盖37/37；正式有效诊断与解法审核覆盖2/2，对正式固定分母的过程审核覆盖2/9。没有未决的人工作品结论。

## 7. 典型案例与失败模式

**成功案例 s005。** 候选把任务时长降序排列。Hy3 正确指出这会增加总等待，定位 `sort(a.rbegin(),a.rend())`，提出 `2 / 1 10` 反例并给出升序修正；人工确认且参考代码3/3测试通过。

**正确案例 s010。** Hy3 正确识别“每次合并最轻两堆”的 Huffman 贪心，没有误报；参考代码3/3测试通过，人工确认过程成立。

**主要失败不是传输，而是输出契约。** s004/s011/s012/s023缺字段，s024达到长度上限后留下非法JSON，s006/s037达到上限但内容为空。严格校验防止畸形结果进入指标，却也暴露 Hy3 在长推理与深层JSON结构上的可靠性问题。

## 8. 威胁、边界与安全

- 9条是小规模、一次随机响应的冻结实验，不代表 Hy3 总体能力。
- 公开经典贪心思想可能受模型记忆影响；材料原创表述只能降低直接复述风险。
- 固定测试覆盖有限，性能类和证明类结论依赖独立分析；测试通过不等于形式化正确。
- Gold、难度与人工过程判断具有主观性，虽由项目作者确认，仍非多人一致性标注。
- 网页静态诊断不执行用户代码；隔离 worker 仅用于事先审查的仓库/模型评测代码，不是通用强沙箱。
- 无自动重试、无OJ、无外网开放；原始响应、请求、账户确认和Key保留在Git忽略目录。
- 项目自有代码与文档采用 MIT；`third_party/`及来源材料保留自身许可/来源，不被重新授权。

## 9. 复现

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure

# 纯离线重放正式结果；不调用Hy3
build/Release/hy3_evaluate.exe formal-report `
  evaluation/formal-20260909/freeze-manifest.json `
  evaluation/results/formal-20260909-records.json `
  reproduced-report.json `
  evaluation/results/formal-20260909-solution-evidence.json `
  evaluation/reviews/formal-result-review-20260909.json
```

真实交互应用需要自行设置服务端 `TOKENHUB_API_KEY`；仓库只提供空值示例。正式实验无需也不应重发，公开脱敏记录可以离线重算报告。

## 10. 交付索引

- 应用与快速运行：[README](../README.md)
- 评测协议：[evaluation v2](evaluation-v2.md)
- 正式冻结：[freeze manifest](../evaluation/formal-20260909/freeze-manifest.json)
- 脱敏逐样本结果：[records](../evaluation/results/formal-20260909-records.json)
- 解法执行证据：[solution evidence](../evaluation/results/formal-20260909-solution-evidence.json)
- 机器可读正式报告：[report](../evaluation/results/formal-20260909-report.json)
- 两轮真人审核：[材料](../evaluation/reviews/) / [正式结果](../evaluation/reviews/formal-result-review-20260909.json)
- 94秒真实结果回放：[GIF](assets/hy3-algotrace-real-replay.gif)
- 逐项验收：[submission acceptance](submission-acceptance.md)

发布候选 `620af9be3457df51990d0877546b72a17aca3ab1` 已通过 PR #1 合并为 `fd6fd7fe68580227b0f554dd7d0269808db8d856`；对应最终 [main CI](https://github.com/Smily2333/hy3-algotrace/actions/runs/34839459235) 的 Windows/Ubuntu 均通过。发布索引见 `SUBMISSION.md`。
