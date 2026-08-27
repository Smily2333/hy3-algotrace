# M2 评测准备与隔离验证（2026-08-27）

本次按连续 M1–M4 授权执行。M1 检查点 0836f0f064ec49779a8c6a3759e24fd36e9a688d 已推送，
[CI 33056104253](https://github.com/Smily2333/hy3-algotrace/actions/runs/33056104253) 对应 SHA 的 Windows/Ubuntu 均 success。
M1 历史 journal 的“等待启动”只描述当时结束点，当前入口以 roadmap 为准。

## 实际完成

- 8 原创表述题/25受控候选；开发2题7条、保留6题18条。每条独立 gold、过程标签、位置和来源。
- 独立 evaluation-v1 响应封装，附完整 C++17 源码，不静默改网页 v2。
- 复用 v2 严格校验器；allowlist 输入投影、导出hash、raw离线导入、报告与答案证据接入。
- max_tokens 可选适配（旧默认不变）；累计预算预留/对账/未知保留/锁/重复拒绝/38次硬上限。
- 真实开发入口限3次，账户条件默认为拒绝，保留集调用明确拒绝。
- Linux/WSL bubblewrap 隔离工具；无网络、清空环境、不挂载home/主机树/Key；
  C++ argv 编译执行、超时进程组终止、输出/地址空间/文件/进程限制。
- 独立小规模穷举/排列/递归/DP oracle 对固定 expected output 交叉核对。

## 环境与真实证据

Docker Desktop 启动后仍无 Linux engine，未用普通宿主进程替代隔离。
按权限流程在现有 Ubuntu WSL 安装 bubblewrap/g++，先跑空任务隔离探针成功。
实际编译器 GCC13.3.0；然后在隔离中执行8参考+25候选，33个均编译成功。
参考8/8通过，候选10/25通过、15/25输出不符；固定测试由独立 oracle 全部确认。
s024平方复杂度代码小测试通过；s025代码通过但文字证明不成立。
这不是 Hy3 质量指标，也不是充分测试或人工复核。

原始本地证据：build/m2-answer-results-hashed.json，SHA256：
4cd82c553e3edab5216d32fce6f444c2cd3b392a41f5589949443e64e09d6773。
脱敏证据保留相对材料身份、源码/输入hash、实际有限输出、耗时及结果，
删除编译器本机路径和冗余编译文本，见 evaluation/results/fixed-answer-evidence.json。

6个受控 worker fixtures 分别得到 passed / wrong_answer / compile_error /
execution_failed / timeout / output_limit_exceeded，符合预期。
结果在 build/m2-worker-fixture-results.json；不是模型响应。

## 实际验证命令

~~~text
cmake --build build/m1 --config Release --parallel 4
ctest --test-dir build/m1 -C Release --output-on-failure
ctest --test-dir build/m1 -C Release -R evaluation_tests --output-on-failure
build/m1/Release/evaluation_tests.exe .
build/m1/Release/hy3_evaluate.exe validate evaluation/materials/dataset.json
build/m1/Release/hy3_evaluate.exe export evaluation/materials/dataset.json build/m2-export
build/m1/Release/hy3_evaluate.exe jobs evaluation/materials/dataset.json build/m2-jobs-oracle.json
build/m1/Release/hy3_evaluate.exe report evaluation/materials/dataset.json evaluation/fixtures/not-attempted.json build/m2-synthetic-report.json evaluation/results/fixed-answer-evidence.json
node --check web/app.js
git diff --check
~~~

本机使用已有便携CMake4.3.4/MSVC。完整 build/CTest **12/12通过**；
随后分层报告/元数据小改只重跑受影响测试，**36/36通过**。
账户示例拒绝测试返回 E_EVALUATION: account allowance confirmation missing，
没有创建调用目录、没有读取 Key 或模型 invocation。
Linux独立worker编译无警告（已修复两处缩进警告）；M2远端CI结果另见交付记录。

## 未完成与边界

- 真实请求0，实际token0、未知0，300000预算尚未使用。未从公开文档推断账户余额。
- 需核实账户当前可用额度与不会额度外扣费，之后才允许开发试跑。
- 正式协议冻结在开发试跑后；当前材料为 draft，不宣称正式评测完成。
- 尚未指定人工审查者，所有过程gold均 agent_authored_pending_human。
- 模型完整解答独立执行、实际人工签收导入、正式批次编排及真实结果分析未完成。
- 没有顶层LICENSE，未擅自补许可证。未访问OJ、未恢复通用CandidateRunner。
- 旧data/、旧Prompt/Phase2协议指标与9条pilot保持不变；本次共享改动仅max_tokens与公开校验入口。
