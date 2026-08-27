# M4 可交付部分与最终验证

日期：2026-08-27。分支：codex/interactive-diagnosis-ui。

## 检查点

- M1：0836f0f064ec49779a8c6a3759e24fd36e9a688d，已push；
  [CI 33056104253](https://github.com/Smily2333/hy3-algotrace/actions/runs/33056104253)，Windows/Ubuntu success。
- M2材料/工具：ca2d7ba53ae10b61681ae7423489fedf988a2ffe，已push；
  [CI 33083711638](https://github.com/Smily2333/hy3-algotrace/actions/runs/33083711638)，Windows/Ubuntu success。
- 后续跨平台修复：59a7689，评测扩展模板与hash统一LF，37项评测回归通过。
  该修复不修改Prompt语义、材料或历史结果。
- 最终代码及交付检查点：dc26ed30e4eca1e5f3d1e18dbd06a00486ad1bd7，已push；
  [CI 33084266081](https://github.com/Smily2333/hy3-algotrace/actions/runs/33084266081)
  对应此SHA，Windows（2m25s）/Ubuntu（1m23s）均success，configure/build/12项CTest/CLI全部通过。
  本记录随后仅做文档补记，不将文档补记SHA冒充已跑CI的SHA；没有重复同等全量测试。
  CI有既有checkout@v4的Node20弃用提示，不影响本次成功，未扩大修改workflow。

## 实际交付

- README/roadmap/architecture同步，不再等待旧M1/M2授权。
- docs/evaluation-v1.md：响应版本、材料、独立答案工具、命令、预算与账户门槛。
- docs/delivery-report.md：背景、设计、实际证据、两类评估区别、有效性限制及待办。
- evaluation/materials/dataset.json：8题25候选；旧data/不变。
- evaluation/results/fixed-answer-evidence.json：33个实际隔离编译/运行记录，已脱敏。
- evaluation/delivery-manifest.json：模型/协议/数据/代码版本、LF哈希与0调用预算记录。
- evaluation/review-queue.json：25条真人待审表，未伪造审查者或日期。
- docs/demo-m1-m4.md、docs/assets/m1-fake-ui.png：110秒脚本、复现步骤及实际Fake页面截图。
  未发现ffmpeg，现有浏览器工具不提供视频录制；**尚缺视频/GIF成片**。
  完整页截图有拼接重复，未纳入交付。

## 本地验证

完整CMake构建成功、CTest12/12；随后评测分层/换行小改只重建受影响目标，
evaluation_tests37/37。JavaScript语法通过。M1六场景浏览器Fake记录复用，不冒充本次真实模型。
6个隔离worker fixtures全部符合预期。8参考全部通过并有独立oracle核对；
25候选10通过、15输出不符。原始运行材料只留build忽略目录。

冻结集合：data/、旧evaluator/interactive-v1 Prompt、Phase2协议指标和pilot结果与接手基线零diff。
最终检查6份当前入口文档的本地文件链接：0个断链（不包含远端可用性或锚点逐项检查）。
未新增Python文件、未提交真实模型raw或Key；项目新增源码/材料未含用户绝对路径。
M2仅有synthetic占位密钥测试，账户确认示例默认为拒绝；真实Key未加载，真实调用0。
本轮没有OJ、main合并、tag、Release或活动链接提交。

## 外部依赖

- 当前账户可用额度及不会额度外扣费的控制台确认。授权上限300000不等于账户余额。
- 真人审查者/日期/过程与定位复核，待作者安排。
- 开发试跑后正式冻结、正式调用与模型代码独立验证；当前正式纳入0。
- 视频成片与项目顶层许可决策。

M1完成；M2核心技术准备已交付但仍有冻结/人工缺项；M3未完成；M4部分交付。
不得据此标整体完成或模型效果验收通过。
