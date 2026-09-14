# hy3-algotrace — 任务 2 提交包

公开仓库：<https://github.com/Smily2333/hy3-algotrace>

一句话介绍：面向贪心算法学习者的 Hy3 + C++17 本地诊断应用，可从完整题面与代码生成算法概述、首次错误定位、反例和完整修正解法，并用冻结评测验证输出可靠性。

本仓库是 Smily2333 的个人开源实践 / 参赛作品，对应《犀牛鸟开源-实战任务-混元大语言模型项目》任务 2；不是腾讯、混元或题目来源方的官方项目。

## 快速运行

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure

# 设置自己的服务端Key后启动本地网页；不要把Key写入仓库
$env:TOKENHUB_API_KEY = [Environment]::GetEnvironmentVariable('TOKENHUB_API_KEY','User')
build/Release/hy3_algotrace_demo.exe --host 127.0.0.1 --port 8080
```

## 交付入口

- [最终实验与分析报告](docs/delivery-report.md)
- [12题37候选材料](evaluation/materials/dataset.json)与[扩充材料](evaluation/expansion-20260828/dataset.json)
- [正式9条冻结清单](evaluation/formal-20260909/freeze-manifest.json)
- [脱敏逐样本结果](evaluation/results/formal-20260909-records.json)与[机器指标](evaluation/results/formal-20260909-report.json)
- [人工审核记录](evaluation/reviews/)
- [94秒真实结果回放 GIF](docs/assets/hy3-algotrace-real-replay.gif)
- [任务书逐项验收](docs/submission-acceptance.md)

## 发布验证

准备发布的实现提交与对应CI将在最终双平台验证后记录于此；默认分支当前状态以 GitHub `main` 为准。

## 已知限制

只支持贪心题；网页只做静态分析。正式样本仅9条且7条输出契约失败，不能外推为Hy3总体能力。固定测试不是形式化证明；隔离执行只处理事先审查代码，不是通用强沙箱。原始响应、账户确认和Key不公开。

## 活动平台可复制信息

- 标题：`hy3-algotrace：基于混元 Hy3 的贪心算法解法过程诊断`
- 简介：`一个C++17本地网页应用：输入完整题面和代码，Hy3返回算法概述、首次错误定位、反例及完整修正解法；附12题37候选材料、冻结9条真实实验、人工审核、隔离答案证据和94秒Demo。`
- 仓库：`https://github.com/Smily2333/hy3-algotrace`

活动平台的最终提交按钮仍由项目作者本人点击。
