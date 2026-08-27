# M3 v2开发验证与独立数据扩充（2026-08-28）

用户在离线v2和双平台CI通过后授权继续，并允许联网适度扩充。执行基线 `e3cd81e`；本轮模型请求使用已验证的 `c72590c` 代码、同一数据和v2模板，期间未调Prompt/温度/模型/评估规则。新增材料在独立目录，不改变本轮3条的分母。

## 三次真实验证

| 样本 | HTTP / finish | 严格解析 | 诊断 | 输入/输出/总token | 耗时ms | 完整代码固定测试 |
| --- | --- | --- | --- | --- | --- | --- |
| s001 | 200 / stop | parsed | correct / null，与gold一致 | 2304 / 11853 / 14157 | 109988 | 3/3通过 |
| s002 | 200 / stop | parsed | incorrect / boundary_omission，与gold一致；第9行 | 2304 / 12484 / 14788 | 92430 | 3/3通过 |
| s003 | 200 / stop | schema_invalid | 不纳入成功诊断 | 2302 / 8829 / 11131 | 63987 | 无合法代码对象，未执行 |

新增3次、无重试，6910输入+33166输出=40076 token；reasoning 30134是输出子集，cached为0，不能重复计费相加。累计v1+v2为6次、76496 token，未知0，剩余223504 token/32次总上限。**六次开发上限已用完，不自动开始正式保留集或第三轮调Prompt。** 旧v1三次失败和36420 token完整保留。

脱敏用量/模板与数据hash/原始响应SHA见 [summary](../../evaluation/results/development-v2-summary.json)。原请求和raw仅在忽略目录 `build/m3-development-20260827/v2-s001` 等；原账本追加v2-* reserve/done，不重置目录。实际provider request_id仅本地保存。精确Key及Authorization/Bearer扫描无命中。

## 严格报告与限制

沿用现有Reporter；分析范围是调用前已选定的s001–s003投影，包含失败，不是从25条中事后挑成功样本。投影保留原始材料hash，模型请求本身未改变。完整报告：[JSON](../../evaluation/results/development-v2-report.json)；实际答案证据：[answer-evidence](../../evaluation/results/development-v2-answer-evidence.json)。

| 冻结口径 | 分子/分母 | 值 |
| --- | --- | --- |
| 候选诊断一致率 | 2/3 | 0.6667 |
| 完整解答固定测试答案准确率 | 2/3 | 0.6667 |
| 完整解答答案验证覆盖率 | 2/3 | 0.6667 |
| 首次错误定位 | 1/2 | 0.5000 |
| 过程正确候选误报率 | 0/1 | 0.0000 |
| 完整解答人工过程确认正确数 | 0/3 | 报告原始值0；尚无人工确认，不代表三条过程全错 |
| 完整解答人工审查覆盖率 | 0/3 | 0.0000 |

schema_invalid=1、invalid_json=0、undetermined=0、缺完整代码=1。分类：正确gold的1条成功；边界gold的1条成功；策略gold的1条为契约失败。全部来自同一basic开发题，不能给出medium/hard或保留集模型结论。confidence/calibration未估计。

s003原始JSON合法，错误路径 `/solution_code`：对象缺失，counterexample/reference_solution仍位于错误顶层。原文指出升序问题，但first_error/finding定位到第9行，gold为第8行；这是定性定位分歧，**不能补字段、搬层级后计为成功或修改gold**。finish_reason=stop只说明提供方报告正常停止，不能单凭它确认服务端或模型失败根因。

两份合法完整代码经Planner静态执行前审查，只含标准库计算和stdin/stdout，在批准的bubblewrap中编译执行。原对照s001通过、s002/s003各有反例失败，和既有gold一致。模型代码通过有限测试不证明完整过程成立；两份correctness文字都偏重断言贪心最优，仍需人工确认论证充分性。原模型provenance不改成已证明，执行事实另存。

v1的0/3与v2的2/3只能作开发观察；样本复用、单题、单次随机响应等因素使它不能证明模板改动带来普遍质量提升。

## 新增材料与交付

独立 [扩充目录](../../evaluation/expansion-20260828/README.md)：4题12候选、16测试、来源登记与待审表；加上旧材料共12题37候选，但正式纳入仍0。只从大学公开讲义核对概念，没有访问OJ、下载或执行网上代码。旧data、旧Prompt、旧评测材料和历史结果不改。

4份新参考实现全部通过；候选5通过/7输出错误；s037是代码通过但显式证明错误的对照。新材料仍未冻结、未经人工审核、未发给模型。隔离环境无网络/凭证，不声明为通用强沙箱。

本地执行 `cmake --build build/m1 --config Release --parallel 4`、`ctest --test-dir build/m1 -C Release --output-on-failure`：完整构建成功、12/12 CTest通过；evaluation_tests为75/75。受控worker在WSL GNU C++13.3.0下构建，先按独立oracle核对expected再在bubblewrap内执行；新增16个作业、64次测试结果完整保存。文档本地链接及精确Key/凭证/私人绝对路径检查通过；没有新增Python文件。

集成检查点 `47634bc5299745860c2cf41e8203d32db7ed018b` 对应 [CI 33093371830](https://github.com/Smily2333/hy3-algotrace/actions/runs/33093371830)：Windows/Ubuntu configure、build、全部CTest、CLI数据校验均成功。此后仅补写本段CI证据，不重复CI。旧冻结集合零diff，原始响应与运行目录未提交。

M1保持已完成；M2增加独立草案，待人工与正式冻结；M3开发验证有结果但契约仍失败1/3，不能宣称完成；M4更新公开摘要，视频与最终审核仍未完成。下一步优先由真人复核两份模型解法论证及s003的失败/定位分歧，再复核新增gold；不能将剩余额度视为绕过六次开发上限的许可。
