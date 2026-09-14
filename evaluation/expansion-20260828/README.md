# 独立扩充草案：4题、12条候选

2026-08-28用户授权适度扩充。此目录不替换旧8题25候选，不进入本轮模型调用。合计现有12题37候选，但**不是一个已冻结、已人工审查或已跑模型的37样本基准**。

## 来源与内容

联网核对大学教学材料（使用PDF阅读流程核对算法条件），不访问OJ，不下载/执行网络代码，不复制题面、图表或参考程序。中文题面、输入约束、代码、变体和测试由Planner编写；不是原算法发明，也不是大学官方评测数据。完整作者、页码、访问日期、改写说明及未采用来源见 [sources.json](sources.json)。

| 题号 | 任务 | 草案分层 | 候选 | 来源 |
| --- | --- | --- | --- | --- |
| p09 | 最少并行工位，全部任务必须安排 | medium / development | s026–s028 | [Princeton讲义](https://www.cs.princeton.edu/~wayne/kleinberg-tardos/pdf/04GreedyAlgorithmsI.pdf)，第17–23页 |
| p10 | 最小最大延迟，非最小总等待时间 | medium / holdout | s029–s031 | 同上，第25–32页 |
| p11 | 闭区间最少检查点，允许单点/重复 | basic / development | s032–s034 | [Williams课程作业](https://www.cs.williams.edu/~shikha/teaching/fall19/cs256/assignments/CS256_Assignment_3.pdf)，题1–2 |
| p12 | 初始空缓存、离线序列最少装载 | hard / holdout | s035–s037 | Princeton讲义，第38–52页 |

拆分尚未冻结。p09/p11与旧开发p03都属于区间家族，放开发侧；p10与旧保留p06属于期限调度家族，保留侧保持一致。不能随机按代码变体拆分，也不声称不同任务之间完全独立。公开经典算法存在训练集记忆风险；难度为工程判断，不是官方评级。

## 标注与验证

- [dataset.json](dataset.json)：每题完整stdin/stdout契约、参考C++17、证明摘要、复杂度和4个固定测试；每题3候选。过程标签为4正确/8错误，均待人工审核。
- [review-queue.json](review-queue.json)：12条待审；原s001–s003的人工结论不自动延伸到这里。
- [answer-evidence.json](answer-evidence.json)：隔离编译运行证据；4参考均通过，12候选为5通过/7输出不符。全部64次测试执行均留有结果，测试输入和源码SHA绑定。
- 16个固定expected output已由独立小规模穷举核对：工位枚举分配、延迟枚举全排列、检查点枚举端点子集、缓存枚举所有淘汰选择。代码在 `evaluation/tools/independent_oracle.hpp`，n<=10，不能用于大规模性能结论。
- s037代码与s035相同、全部固定测试通过，但reasoning声称任意淘汰都同样最优；t1中可产生4次或6次装载，反驳该断言。首错在文字，不伪造代码行号。
- gold.answer_status保留unverified，真实答案状态通过独立evidence关联；不把运行结果回写gold，不把小测试通过当作完整证明。

测试包含闭/开端点、重复区间、峰值与末状态差异、64位累积及缓存命中/淘汰。每题仅4例，尚缺性能压力与更全面生成测试；参考证明也需人工复核。未提供第三方材料的新许可，不分发原PDF。

## 复现

从仓库根目录，使用已有hy3_evaluate及批准的Linux/WSL隔离launcher：

```text
hy3_evaluate validate evaluation/expansion-20260828/dataset.json
hy3_evaluate jobs evaluation/expansion-20260828/dataset.json build/new-expansion-jobs.json
sh evaluation/tools/run-isolated.sh build/fixed-answer-worker-expansion build/new-expansion-jobs.json
```

worker通过已有CMake Linux目标或C++17编译器构建，必须带当前独立oracle。禁止直接在普通主机运行worker或候选。模型API没有接收新增材料。
