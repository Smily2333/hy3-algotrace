# 贪心离线评测 v1（M2）

状态：工具与 8 题/25 候选已实现；固定答案隔离验证通过。**尚未正式冻结、没有真实 Hy3 实验、没有人工审查。**
网页继续使用 interactive-request-v2 / interactive-diagnosis-v2；不更改其默认模板或历史结果。

## 版本和数据隔离

- 材料：evaluation/materials/dataset.json，greedy-dataset-v1，m2-draft-20260827。
- 评测输出：greedy-evaluation-v1，严格三个字段：schema_version、diagnosis、solution_code。
- diagnosis 是原 v2 完整对象，复用同一严格校验器。
- solution_code 固定 availability、language=cpp、standard=c++17、source_code、unavailable_reason。
  provided 必须非空完整源码且 reference_solution 为 provided；unavailable 必须 null 源码和非空原因。
  代码字段只验证格式/长度，不声称编译成功。非法 JSON/fence 不修复。
- 输入：题面、候选代码、可选用户 reasoning；没有 gold、参考实现、测试答案、难度、split、构造备注。
  所有请求使用中性 request_id=input；本地文件名 sample ID 不进入 Prompt。调用账本另用唯一 attempt ID。
- Prompt 是冻结的网页 v2 模板加 prompts/hy3-greedy-evaluation-v1.md 封装指令，
  不是伪装成 v2 的独立模型响应。export manifest 保存两模板、材料与每个 Prompt 的哈希。
- 新材料不写入旧 data/；旧实验不混入本评测。当前未使用保留题调 Prompt。

## 材料与独立标签

8 题：数额选择、等待安排、单厅预约、合并账单、递增补齐、期限内任务、最低补给、容量配对。
题面和实现由 Planner 原创编写，使用经典贪心算法思想；未复制第三方题面，没有访问 OJ。
构造来源逐条记录，**不是人类编写/审核**。仓库未发现顶层 LICENSE，不擅自添加或改变许可；
公开 GitHub 可读不等于已经授予第三方再分发许可，需项目作者后续决定许可。

| 集合 | 题目 | 候选 |
| --- | --- | --- |
| development | p01、p03 | 7（含 s025 错误证明） |
| holdout | p02、p04～p08 | 18 |
| 合计 | 8 | 25 |

基础 2 题/7 候选，中等 3 题/9 候选，困难 3 题/9 候选。
分层是工程判断，依据交换论证、不变量和实现难度；不是平台评级或任务书硬性指标。
每题3个固定测试，p07为4个，共25个；测试规模小，不代表覆盖全部边界或性能。

gold 独立记录代码/显式 reasoning 整体的过程标签。s025 代码算法成立但显式证明错误，
首次错误在文字中，不硬配代码行。s024 平方排序在小测试通过但十万规模复杂度不成立。
原始 gold.answer_status 保持 unverified；动态答案事实来自独立哈希绑定证据，不回写 gold。
主类别按最直接缺陷标注；附加类别只记录独立缺陷，不机械复制下游后果。本轮每变体一个根因。

## 命令

先 canonical CMake 构建 hy3_evaluate；Windows 命令前缀通常 build/Release/，Linux 为 build/。

~~~text
hy3_evaluate validate evaluation/materials/dataset.json
hy3_evaluate export evaluation/materials/dataset.json build/new-export
hy3_evaluate jobs evaluation/materials/dataset.json build/new-jobs.json
hy3_evaluate import evaluation/materials/dataset.json s001 RAW_JSON build/new-record.json
hy3_evaluate report evaluation/materials/dataset.json RECORD_BUNDLE build/new-report.json [ANSWER_EVIDENCE]
~~~

输出文件/导出目录必须不存在，不覆盖、不自动重试。import 保留原始响应 hash，不补全模型源码。
记录包形状为 {"data_kind":"synthetic 或 real","records":[逐样本导入记录]}。
report 重新校验所有声称 parsed 的响应；执行状态只能通过独立证据文件导入。
证据必须匹配材料 SHA、候选/模型源码 SHA、全部测试 ID、输入 SHA、expected output；
实际 stdout 重新比较，不相信一个 passed 字符串。人工过程结论尚未实现签收导入，保持 unreviewed。

### 指标

全部预纳入样本为分母，失败/无法确定/缺代码不剔除。
最终解答答案准确率 = 模型代码全部指定测试通过数 / 纳入数，另报执行覆盖率；
过程正确率 = 独立人工确认解法正确数 / 纳入数，另报审查覆盖率（当前没有人工）。
候选诊断一致率按独立 process_status；过程正确误报率只以过程正确样本为分母。
定位分母为动态 wrong_answer 且有 gold 位置的候选；命中须判 incorrect，
首错 finding 类别相同且完整行范围包含于 gold 范围，不比较随机步骤 ID。
这只是预定义自动位置匹配，不证明自然语言根因一致，需人工复核。

零分母 value=null。报告输出计数/分子/分母、失败、undetermined、缺解答、开发/保留、
难度与类别分层计数，以及人工复核队列。未开始真实实验时不得把 synthetic 数字作为模型指标。

优先人工复核全部答案正确告警；其余漏判、所有困难题及过程正确样本加入队列。
reviewer/date/evidence 保持 null、decision=pending，禁止智能体冒充人工。
正式冻结与完整人工结果导入仍是前置缺项；CLI 当前拒绝保留集真实调用。

## 最小答案校验与安全

仅 Linux/WSL，使用 evaluation/tools/run-isolated.sh。Windows 原生支持应用与离线报告，不支持原生候选执行。
先安装 g++ 和 bubblewrap（实际安装须批准），构建 fixed_answer_worker（Linux CMake target）。
在维护者审查输入后运行：

~~~sh
sh evaluation/tools/run-isolated.sh build/fixed_answer_worker build/new-jobs.json > build/answer-results.json
~~~

shell 仅传递两个已解析的路径；编译/运行由 C++ fork/execve 的 argv 完成，不拼接代码到 shell。
bubblewrap 隔离 user/pid/network 等命名空间、清空环境、只读挂载系统运行库与两个输入文件；
不挂载 home、主机工作树、网络配置、密钥或 Docker socket。清空 capability，独立 session。
每次临时目录在私有 /work tmpfs 内创建，清理仅删本次 canonical 核验的目录。
tmpfs 256 MiB、每进程地址空间512 MiB、文件2 MiB、进程数128、core=0；
编译20秒、每测试2秒、批次900秒。每流64KiB，超限终止进程组；子进程退出后也清理同组后代。
内核/namespace 隔离不是虚拟机，不宣称完整强沙箱；不面向恶意任意代码或公网。
未来模型代码必须另经审查批准后才能进入同一工具，不能从 HTTP 响应自动执行。

比较仅 CRLF→LF，允许一个末尾 LF 差异；保留尾空格、额外空行及其它字节。
报告记录第一差异字节、有限实际输出、退出码/信号、耗时和截断。
参考答案另由独立小规模子集/排列穷举、递归或 DP 核对，不由参考程序自证。
执行证据见 evaluation/results/fixed-answer-evidence.json。

## 真实调用预算与阻塞

已授权本轮合计300000 token、38次，失败也计次，所有开发/正式/演示共用同一账本。
当前**0次、0 token**。官方查核：
- [协议字段](https://cloud.tencent.com/document/product/1823/135872)：max_tokens 是输出上限；思考计入 completion_tokens。
- [Hy3 指南](https://cloud.tencent.com/document/product/1823/132252)：最大输入192k，推理与回答共享输出额度。
- [计费方式](https://cloud.tencent.com/document/product/1823/130054)：输入、输出、缓存输入计费，后付费结算存在延迟。

适配器新增可选 max_tokens，unset 保持旧行为。评测试跑设16384；
每请求保守预留196608+16384+1024=214016，不靠未经验证的字数/token比。
过度保守可能导致提前停止，不要求耗完额度。usage 的 prompt+completion 必须等于 total 才对账；
缺失/不一致保留整个预留，报出越界即 halt。追加式 reserve/done 文件和目录锁，
崩溃后不自动释放、不重发；最多38次；开发入口另限3次。

在真实调用前，需项目作者确认**当前账户可用额度和不会产生额度外扣费**。
公开资料不能证明账户余额；本轮未读取真实 Key、未发出测试请求猜余额。
evaluation/account-confirmation.example.json 默认拒绝调用。确认文件应只保存在忽略目录；
服务端从已配置 TOKENHUB_API_KEY 环境读取，不在命令/网页/日志显示密钥。
现有 call 入口仅开发集三次试跑；正式自动调用在冻结与预算方案完成前明确关闭。
