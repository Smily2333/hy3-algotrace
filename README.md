# hy3-algotrace

> 个人开源实践 / 参赛实验：基于混元（Hy3）的算法竞赛解法推理过程评估研究

**当前状态：`phase1b_planner_reviewed_pending_github_ci` — C++17 数据契约校验器（`hy3_algotrace validate`）已实现并通过本地功能验证（MSVC，56/56 测试通过），Phase 1B-R2.1 已通过 codex_planner 技术验收，当前等待 GitHub CI 验证 canonical CMake/CTest 与 Windows/Linux。** 数据集（数据契约 0.3.0，3 题 × 3 轨迹 = 9 条模型生成样本）已通过 Codex 规划方（codex_planner）技术复核（review_status=planner_reviewed，2026-08-23）；此复核仅代表规划方技术验收，不等同于人工（human_reviewed）或专家（expert-reviewed）审查背书。校验器**仅做数据结构与契约一致性校验**（schema / manifest / 外键 / 诊断规则 / 计数重算），**不**调用模型 API、**不**连接外部 OJ、**不**执行任何候选代码。构建方面：CMake 配置（canonical、跨平台）已就绪但本机无 CMake 未实际运行；本地功能验证使用现有 MSVC `cl.exe`（详见 `docs/journal/phase-01b.md`，`cmake_ctest_status = unverified_tool_unavailable`，`cross_platform_status = unverified`）。

> ⚠️ **项目性质声明**：本仓库是**个人开源实践 / 参赛项目**，**不是**腾讯、腾讯混元（Hunyuan）或 Codeforces 的官方仓库，也**不代表**任何官方立场或背书。其中由 Hy3（混元）模型生成的部分推理样本，由本仓库维护者自行产出并标注 `model_generated`，不代表腾讯或混元的官方意见。计划公开仓库地址：<https://github.com/Smily2333/hy3-algotrace>。

## 1. 项目目标

hy3-algotrace 是一个个人开源实验（参赛项目方向）：给定一道**算法竞赛题**、相关**测试信息**，以及由模型（Hy3）生成的 C++17 **解法思路**，系统评估其：

- 算法选择是否合适；
- 正确性论证（尤其是贪心题的贪心性质 / 交换论证）是否成立；
- 复杂度分析是否正确；
- 边界条件是否考虑周全；
- 实现与思路是否一致；

并**定位推理过程中的错误所在环节**，而非仅判定最终代码 AC/WA。

## 2. 初始版本范围（Phase 0 冻结）

- 技术方向：**C++17**，不使用 Python。
- 仅研究**贪心算法题（greedy）**。
- 暂不实现：搜索、动态规划、图论等其它算法类型（仅作为未来扩展记录）。
- 本阶段**只**建立可执行的项目基础与文档约定，**不实现完整评测算法**。

## 3. 概念输入与输出形式

输入（概念形式，文档约定，本阶段不读取 / 不解析）：

- **题目记录**：题面、约束、来源、参考标签、算法类型。
- **候选推理轨迹**：参赛者对本题的解题推理，按六环节分段（题意理解 → 贪心策略选择 → 贪心性质 / 交换论证 → 复杂度分析 → 边界条件 → 实现与思路一致性），并标注来源（人工编写 / 模型生成 / 改写）。
- **候选 C++17 解法**：参赛者给出的参考实现（可选，用于后续一致性验证）。
- **测试信息（test_case）**：可选测试用例（输入 / 期望输出 / 来源 / 用途），用于后续验证。

输出（概念形式）：

- **诊断报告**：针对每条推理轨迹，给出诊断状态（`correct` / `incorrect` / `undetermined`）、主要错误类别（`primary_category`）以及多条具体发现（`findings`，含出错环节定位、依据与改进建议）。错误类别见 `docs/error-taxonomy.md`。

> 本阶段仅定义以上形式，不实现解析器或评测器。数据字段详见 `docs/data-contract.md`（含 test_case、verification_result 与可复现元数据）。

## 4. 为什么先测试贪心题

1. **推理可解释性强**：贪心题的「贪心策略选择 + 交换论证」是相对自洽、易拆分的推理链，便于人工标注与评测校准。
2. **错误边界清晰**：贪心错误（如选错贪心量、缺失 / 失效交换论证）类型明确，适合作为错误分类体系的种子。
3. **低风险起步**：不涉及复杂状态转移，便于先把「过程评估」方法论跑通，再扩展到 DP / 搜索 / 图论。
4. **数据可得**：Codeforces 等平台有稳定且丰富的经典贪心题。

## 5. 数据来源与预计规模

- 本批（Phase 1A）试验题面**直接来自 Codeforces 官方页面**（160A / 545D / 1398B 三个官方题目页），仅做中文摘要与官方链接引用，不整段复制完整题面。
- 后续扩充候选来源：以 **CodeContests Verified**（含 Codeforces 来源）等数据集为主（尚未下载，仅规划）。注意：该数据集是**后续阶段**的候选来源，不能混为本批（Phase 1A）实际来源。
- 初始选题：约 **12 道** 贪心题。
- 每题构造：约 **6 条** 显式解题推理轨迹（含正确与典型错误样本）。
- 预计规模：约 **72 条** 实验样本（试验基线，非硬性上限）。
- 样本构造：允许基于公开题面由人工编写推理轨迹，但须记录来源、构造方式与标注信息，不得把人工改写伪装成官方内容。
- 标注方式：初版由人工标注算法类型为 `greedy`，后续阶段（Phase 6）再增加自动路由。

## 6. 后续阶段概览

宏观路线详见 `docs/roadmap.md`：

| 阶段 | 主题 |
| --- | --- |
| Phase 0 | 范围与骨架（本阶段） |
| Phase 1 | 数据契约及少量人工样本 |
| Phase 2 | C++17 基础评测管线 |
| Phase 3 | 贪心题小规模实验 |
| Phase 4 | 错误定位与评分校准 |
| Phase 5 | 扩大数据规模 |
| Phase 6 | 自动算法类型路由 |
| Phase 7 | 扩展到搜索、动态规划等类型 |
| Phase 8 | 总结、复现实验与最终交付 |

## 7. 目录结构

```
hy3-algotrace/
├── README.md               本文件（项目说明 + 当前阶段状态）
├── CMakeLists.txt          C++17 校验器 + 测试（canonical 构建；本机未用 CMake 验证）
├── build-msvc/             本地 MSVC 编译产物（git 忽略，非 CMake 产出）
├── docs/
│   ├── architecture.md     系统架构与处理流程（含 Phase 1B 落地模块）
│   ├── data-contract.md    数据契约（语言无关，schema 0.3.0；附录 A 为错误码映射）
│   ├── error-taxonomy.md   错误分类体系 v1（taxonomy 1.0.0）
│   ├── roadmap.md          阶段路线图
│   └── journal/
│       ├── phase-00.md     Phase 0 设计与探索记录
│       ├── phase-01a.md    Phase 1A 数据冻结与首批样本记录
│       └── phase-01b.md    Phase 1B C++17 校验器实现与本地验证记录
├── include/hy3_algotrace/  C++17 头文件（diagnostic / json_loader / validator）
├── src/                    C++17 源文件（main / json_loader / validator）
├── tests/                  validator_tests.cpp（56 项正/负向测试，依赖自由）
├── third_party/nlohmann/   供应商锁定 nlohmann/json v3.12.0 单头文件（MIT）
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

### 8.2 CMake（canonical，跨平台，未在本机运行）

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build        # 运行 validator_tests
./build/hy3_algotrace validate data
```

> ⚠️ 本机无 CMake，`CMakeLists.txt` 未经实际执行验证；标记
> `cmake_ctest_status = unverified_tool_unavailable`。上述命令为 canonical 用法，
> 待规划方在具备 CMake 的环境 / GitHub CI 中补做验证。

### 8.3 本地 MSVC 直接编译（已验证，无 CMake）

本机已用现有 MSVC `cl.exe`（经 `vcvars64.bat` 初始化）完成功能验证：

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cl /std:c++17 /EHsc /utf-8 /W4 /external:I third_party /external:W0 ^
   /I include /I third_party /Febuild-msvc\hy3_algotrace.exe ^
   src\main.cpp src\json_loader.cpp src\validator.cpp
cl /std:c++17 /EHsc /utf-8 /W4 /external:I third_party /external:W0 ^
   /I include /I third_party /Febuild-msvc\validator_tests.exe ^
   tests\validator_tests.cpp src\json_loader.cpp src\validator.cpp
build-msvc\validator_tests.exe data     # 56 passed, 0 failed
build-msvc\hy3_algotrace.exe validate data   # result: PASS (exit 0)
```

> `build-msvc/` 由 `build-msvc/build.bat` 生成，已加入 `.gitignore`，不纳入版本管理。

### 8.4 CLI 用法与退出码

```text
hy3_algotrace validate <data_dir>   校验数据集，打印确定性汇总与诊断；0=PASS，1=FAIL
hy3_algotrace --help | help          打印用法；退出 0
（其它参数）                         打印 E_USAGE 与用法；退出 2
```
