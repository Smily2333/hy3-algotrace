# v2开发验证（不是正式实验）

样本预先选定为s001–s003，单一basic开发题；完整机器报告见 [JSON](development-v2-report.json)，用量与SHA见 [summary](development-v2-summary.json)，分析见 [journal](../../docs/journal/m3-development-v2.md)。未剔除s003失败。

| 指标 | 分子/分母 | 值 |
| --- | --- | --- |
| 契约通过 | 2/3 | 0.6667 |
| 候选诊断一致率 | 2/3 | 0.6667 |
| 完整解答固定测试答案准确率 | 2/3 | 0.6667 |
| 答案验证覆盖率 | 2/3 | 0.6667 |
| 首次错误定位 | 1/2 | 0.5000 |
| 过程正确候选误报率 | 0/1 | 0.0000 |
| 完整解答人工确认正确 | 0/3 | 0（未审核，不能解释为全部错误） |
| 完整解答人工审查覆盖率 | 0/3 | 0.0000 |

s001: parsed/correct；s002: parsed/incorrect/boundary_omission，第9行；s003: schema_invalid，缺solution_code并有字段错层。无JSON语法失败、无undetermined。错误类别分层：边界1成功、策略1契约失败；正确gold1条未误报。没有中等/困难或保留集模型结果，零分母N/A。confidence/calibration未估计。

两份代码各通过3个固定测试不等于所有输入正确；完整论证仍待人审。旧版3次失败未纳入本表但仍计入累计预算。新增40076 token；累计76496，剩余223504。原raw和request_id不公开；单题开发观察不能外推模型总体效果。
