# hy3-algotrace

> 个人开源实践 / 参赛实验：基于混元（Hy3）的算法竞赛解法推理过程评估研究

## 当前执行入口与 8/27 方案

- **接手开发先读：[M1–M4 执行路线图](docs/roadmap.md)。** 包含完整背景、现状、每阶段任务、验收和停止条件，不依赖其他聊天记录。
- **提交材料：[项目方案](docs/project-proposal-2026-08-27.md)。** 包含设计思路、目标架构、重点技术、预期效果与建议排期。
- **历史追溯：[旧 Phase 路线图](docs/roadmap-legacy-phase.md)。** 仅保留历史，不再作为下一步任务入口。

M 表示 Milestone（里程碑）：**M1 做诊断应用，M2 准备评测材料，M3 验证效果，M4 整理交付**。M1 已通过双平台 CI；M2 已交付8题25候选、独立评测工具与隔离答案证据。真实实验和人工复核尚未完成。集中结果见 [阶段交付报告](docs/delivery-report.md)。

**当前状态：M1完成；M2技术准备已交付，M3开发试跑后停止扩大，M4部分交付（2026-08-28）。** 三条真实开发响应均HTTP200，但严格契约通过0/3，总消耗36420 token；不是正式模型效果验收。作者已确认s001–s003的gold，余22条待审。已更正此前片段转义错误的描述（解码后8/8精确匹配）；独立[评测v2](docs/evaluation-v2.md)先离线验证，尚无新版真实调用。详见[试跑记录及更正](docs/journal/m3-development-pilot.md)。网页不执行代码，旧pilot不混入新实验；本次验证见评测v2说明，历史CI仅证明历史代码。

- [评测契约与运行命令](docs/evaluation-v1.md) / [8题25候选](evaluation/materials/dataset.json)
- [真实固定答案证据（不是模型实验）](evaluation/results/fixed-answer-evidence.json)
- [人工待审清单](evaluation/review-queue.json) / [演示脚本与Fake截图](docs/demo-m1-m4.md)

> ⚠️ **项目性质声明**：本仓库是**个人开源实践 / 参赛项目**，**不是**腾讯、腾讯混元（Hunyuan）或 Codeforces 的官方仓库，也**不代表**任何官方立场或背书。其中由 Hy3（混元）模型生成的部分推理样本，由本仓库维护者自行产出并标注 `model_generated`，不代表腾讯或混元的官方意见。计划公开仓库地址：<https://github.com/Smily2333/hy3-algotrace>。

## 1. 项目目标

hy3-algotrace 面向算法学习者，目标是：**输入完整题面 + C++ 代码，获得 Hy3 的错误诊断、代码定位、反例候选和完整参考 / 修正解法**。思路和测试数据可选，不要求用户先编写证明。

本项目同时建设小规模评测材料，独立检查答案正确性、过程是否成立和诊断是否可靠。模型静态判断不等于正确性证明，测试通过也不等于算法对所有输入正确。具体执行范围与验收见 [当前路线图](docs/roadmap.md)。

## 2. 本版交付范围

- 技术方向：**C++17**，不使用 Python。
- 仅研究**贪心算法题（greedy）**。
- 暂不实现：搜索、动态规划、图论等其它算法类型（仅作为未来扩展记录）。
- 复用已有后端、网页和评测管线，不为新目标重写基础设施。
- 网页只做静态诊断；固定题集的受控离线答案校验在 M2 完成，通用 CandidateRunner、沙箱平台和 OJ 接入暂缓。

## 3. M1 输入与输出（交互 v2 已实现）

- **必填输入：** 完整题面（包含约束、输入输出说明）和 C++ 代码。
- **可选输入：** 用户思路、测试数据、补充说明；标题和 I/O 不必拆开填写。
- **默认输出：** 算法概述、未发现明确错误 / 发现错误 / 无法确定、首次错误步骤、代码位置、原因与修改建议。
- **展开输出：** 反例候选、完整参考 / 修正解法（策略、正确性理由、复杂度、边界）；未经执行时明确标为未验证。

当前默认使用 `hy3-interactive-diagnosis-v2`，v1/未版本化请求明确拒绝。最小请求、完整响应示例与位置校验规则见 [交互契约](docs/interactive-diagnosis-demo.md)。旧数据契约、taxonomy 与实验保持冻结；v2 的 `code_logic_error` 只用于交互，不映射成旧指标。

## 4. 为什么先测试贪心题

1. **推理可解释性强**：贪心题的「贪心策略选择 + 交换论证」是相对自洽、易拆分的推理链，便于人工标注与评测校准。
2. **错误边界清晰**：贪心错误（如选错贪心量、缺失 / 失效交换论证）类型明确，适合作为错误分类体系的种子。
3. **低风险起步**：不涉及复杂状态转移，便于先把「过程评估」方法论跑通，再扩展到 DP / 搜索 / 图论。
4. **数据可得**：Codeforces 等平台有稳定且丰富的经典贪心题。

## 5. 数据来源与评测规模

- 本批（Phase 1A）试验题面**直接来自 Codeforces 官方页面**（160A / 545D / 1398B 三个官方题目页），仅做中文摘要与官方链接引用，不整段复制完整题面。
- CodeContests Verified 属于历史候选来源规划，本轮未下载或采用。
- M2 实际为 **8 道原创表述贪心题、25 条受控候选**，开发7条/保留18条，gold由智能体编写待人工复核；10条通过指定测试，15条输出不符，不等于模型准确率。
- 按题目划分开发集和保留测试集，预先确定标准答案、自动校验、过程标签与首次错误位置。
- 旧约 12 题 / 72 样本规划已归档，不再作为本版前置条件；样本量不是任务书的硬性门槛。
- 样本构造：允许基于公开题面由人工编写推理轨迹，但须记录来源、构造方式与标注信息，不得把人工改写伪装成官方内容。
- 本版固定支持 `greedy`，自动路由暂缓。旧 pilot 保留其来源及审查状态，不能冒充新模式正式评测或真实人工抽检。

## 6. 当前里程碑

任务、验收、停止条件和接手指令统一见 [docs/roadmap.md](docs/roadmap.md)。

| 阶段 | 内容 | 当前状态 |
| --- | --- | --- |
| M1 | 两框输入、交互 v2、步骤/代码定位、完整解法 | 双平台程序验收通过；未验证真实模型效果 |
| M2 | 分层样本、独立 gold、最小答案校验与评测适配 | 材料/工具/隔离验证完成；待正式冻结与人工安排 |
| M3 | 真实 Hy3 实验、指标、人工抽检和失败分析 | 旧开发3次均schema_invalid；正式0条；3条gold已人工确认，v2离线验证 |
| M4 | 运行说明、公开材料、分析报告与两分钟 Demo | 报告/证据/脚本已交付；真实结果和视频成片待补 |

## 7. 目录结构

```
hy3-algotrace/
├── README.md               本文件（项目说明 + 当前阶段状态）
├── CMakeLists.txt          C++17 校验器 + 测试（canonical；M1 本地 Windows 已验证）
├── build-msvc/             本地 MSVC 编译产物（git 忽略，非 CMake 产出）
├── docs/
│   ├── architecture.md     系统架构与处理流程（含 Phase 1B 落地模块）
│   ├── data-contract.md    数据契约（语言无关，schema 0.3.0；附录 A 为错误码映射）
│   ├── error-taxonomy.md   错误分类体系 v1（taxonomy 1.0.0）
│   ├── roadmap.md          当前 M1–M4 执行路线图
│   ├── roadmap-legacy-phase.md  已归档的旧 Phase 规划
│   ├── project-proposal-2026-08-27.md  8/27 方案文档
│   └── journal/
│       ├── phase-00.md     Phase 0 设计与探索记录
│       ├── phase-01a.md    Phase 1A 数据冻结与首批样本记录
│       ├── phase-01b.md    Phase 1B C++17 校验器实现与本地验证记录
│       ├── phase-02a.md    Phase 2A 离线评估协议与 Prompt 模板冻结记录
│       └── phase-02b.md    Phase 2B-1 PromptExporter 实现与验证记录
├── include/hy3_algotrace/  C++17 头文件（校验、离线管线、ModelClient/Runner、Hy3 adapter）
├── src/                    对应 C++17 实现与 CLI
├── tests/                  依赖自由单元测试与 synthetic 端到端 smoke
├── web/                    本地交互诊断页面（原生 HTML/CSS/JS）
├── third_party/nlohmann/   供应商锁定 nlohmann/json v3.12.0 单头文件（MIT）
├── third_party/cpp-httplib/ 固定 cpp-httplib v0.51.0 单头文件（MIT）
├── data/
│   ├── manifest.json       数据集汇总（版本/计数/审查状态）
│   └── problems/
│       ├── cf_160A.json    题目 160A Twins 完整样本（含 3 条轨迹）
│       ├── cf_545D.json    题目 545D Queue 完整样本（含 3 条轨迹）
│       └── cf_1398B.json   题目 1398B Substring Removal Game 完整样本（含 3 条轨迹）
└── experiments/            预留：实验记录与结果
```

## 8. 构建与运行

### 8.1 工具作用范围（重要）

`hy3_algotrace validate` **仅做数据结构与契约一致性校验**：加载 `data/manifest.json`
与 `data/problems/*.json`，检查 schema 版本、必需键、ID / 外键、诊断规则、manifest 汇总
计数等。它**不**调用模型 API、**不**连接外部 OJ、**不**执行任何候选代码，也**不**实现
`ProcessEvaluator` / `CandidateRunner`（这些属于 Phase 2+）。

### 8.2 CMake（canonical；本轮 Windows 本地已验证）

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
./build/hy3_algotrace validate data
```

> 本轮已使用官方便携 CMake 4.3.4 与 MSVC 完成 Windows 本地完整构建及 11/11 CTest，实际命令见 M1 记录。Ubuntu 与远端 CI 未为本轮重跑；历史 CI 不替代当前验证。Windows 多配置生成器的 CLI 路径为 `build/Release/hy3_algotrace.exe`。

### 8.3 历史 MSVC 直接编译记录（当前请使用 8.2 CMake）

本机已用现有 MSVC `cl.exe`（经 `vcvars64.bat` 初始化）完成功能验证：

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cl /std:c++17 /EHsc /utf-8 /W4 /external:I third_party /external:W0 ^
   /I include /I third_party /Febuild-msvc\hy3_algotrace.exe ^
   src\main.cpp src\json_loader.cpp src\validator.cpp src\sha256.cpp src\prompt_exporter.cpp
cl /std:c++17 /EHsc /utf-8 /W4 /external:I third_party /external:W0 ^
   /I include /I third_party /Febuild-msvc\validator_tests.exe ^
   tests\validator_tests.cpp src\json_loader.cpp src\validator.cpp
cl /std:c++17 /EHsc /utf-8 /W4 /external:I third_party /external:W0 ^
   /I include /I third_party /Febuild-msvc\prompt_exporter_tests.exe ^
   tests\prompt_exporter_tests.cpp src\json_loader.cpp src\validator.cpp src\sha256.cpp src\prompt_exporter.cpp
build-msvc\validator_tests.exe data              # 56 passed, 0 failed
build-msvc\prompt_exporter_tests.exe data        # 22 passed, 0 failed
build-msvc\hy3_algotrace.exe validate data        # result: PASS (exit 0)
build-msvc\hy3_algotrace.exe export-prompts data prompts\hy3-evaluator-v1.md build-msvc\run_smoke ^
   --run-id smoke-001 --pipeline-commit local-msvc --started-at 2026-08-24T00:00:00Z
```

> `build-msvc/` 由 `build-msvc/build.bat` 生成，已加入 `.gitignore`，不纳入版本管理。
> 这些是历史构建方式，不包含当前全部模块。M1 的本机工具链已实际验证；未修改旧实验或重跑付费调用。

### 8.4 CLI 用法与退出码

```text
hy3_algotrace validate <data_dir>
        校验数据集，打印确定性汇总与诊断；0=PASS，1=FAIL
hy3_algotrace export-prompts <data_dir> <template_file> <run_dir>
        --run-id <id> --pipeline-commit <commit> --started-at <ISO-8601>
        导出每条推理轨迹的评测 Prompt（PromptExporter）；0=成功，1=失败，2=用法错误
hy3_algotrace import-response <run_dir> <trace_id> <raw_file>
        --run-id <id> --generated-at <ISO-8601>
        导入某条轨迹的模型原始响应（逐字节保存），生成 prediction wrapper
        （PredictionImporter）；0=成功，1=业务失败，2=用法错误
hy3_algotrace mark-not-attempted <run_dir> <trace_id>
        --run-id <id> --generated-at <ISO-8601>
        显式将该轨迹标记为 model_call_not_attempted（绝不推断缺文件）；
        0=成功，1=业务失败，2=用法错误
hy3_algotrace report <run_dir> <data_dir>
        --completed-at <ISO-8601|null> --generated-at <ISO-8601>
        生成 report.json + report.md（Reporter，指标严格按 docs/phase-02-metrics.md）；
        0=成功，1=业务失败，2=用法错误
hy3_algotrace --help | help          打印用法；退出 0
（其它参数）                         打印 E_USAGE 与用法；退出 2
```

> 所有命令**不调用模型 API、不连接 OJ、不执行候选代码**。完整离线流程：
> `export-prompts`（生成 prompts/run-manifest）→ 人工把 prompt 交给 Hy3 并取回
> 原始响应 → `import-response` 逐条导入 → 未调用的轨迹 `mark-not-attempted` →
> `report` 生成报告。

### 8.5 Hy3 TokenHub 配置边界（Phase 2C）

- 官方云端默认组合：Base URL `https://tokenhub.tencentmaas.com/v1`、model `hy3`、Bearer API Key；项目默认从 `TOKENHUB_API_KEY` 读取，也支持显式配置注入，任何诊断均不得回显 Key。Key 默认只允许发送到该 HTTPS origin；自定义 HTTPS gateway 必须显式 opt-in。
- `Hy3ModelClient` 固定使用非流式 Chat Completions 与 JSON object 模式；模型内容仍逐字节交给 `PredictionImporter`，不会绕过严格 JSON/schema/语义校验。
- 生产 transport 在 Windows 使用系统 WinHTTP，在 Linux 使用系统 libcurl；默认只接受官方 TokenHub HTTPS origin，验证 TLS，禁重定向和自动重试，并设置明确 connect/total timeout。offline/manual 流程继续可用。
- `call-hy3` 仅从 `TOKENHUB_API_KEY` 环境变量读取凭证，并在发送前原子创建 `model-calls/<trace_id>.json`。任何既存 sidecar/raw/prediction 都会在网络前拒绝重复调用；不得把 `hy3-preview` 或旧平台 `hunyuan-turbos-latest` 当作正式 `hy3`。

### 8.6 本地交互诊断 Demo

```powershell
$env:TOKENHUB_API_KEY = [Environment]::GetEnvironmentVariable(
    'TOKENHUB_API_KEY', 'User')
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target hy3_algotrace_demo
.\build\Release\hy3_algotrace_demo.exe --host 127.0.0.1 --port 8080
```

Linux 或单配置生成器的可执行文件通常位于 `build/hy3_algotrace_demo`。启动后打开
`http://127.0.0.1:8080/`。服务默认且仅允许 loopback，浏览器不会接触 API Key；每次
提交最多调用一次且不自动重试。交互 Prompt、请求/响应契约、长度限制、审计目录和安全
边界见 `docs/interactive-diagnosis-demo.md`。

零费用网页验收请使用该文档的 `interactive_server_tests --serve-fake` 流程；顶部明确标注 Mock/Fake，不能将预设结果当作模型质量证据。

> Demo 当前只支持贪心题，C++ 代码只做模型静态语义审查，既不编译运行，也不代表形式化
> 证明。一次真实 CF 160A smoke 的传输与严格解析成功，但模型漏判了 `>=` 的严格边界错误；
> 该质量失败已如实记录于 `docs/journal/interactive-diagnosis-demo.md`，未重试或修改 raw。
