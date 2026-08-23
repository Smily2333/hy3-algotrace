# Phase 1B 阶段日志 — C++17 数据契约校验器

- **日期**：2026-08-23
- **目标分支**：`main`（尚未提交/推送，待规划方推送）
- **状态**：`phase1b_complete_ci_verified`
  - R2.1 schema conformance 微修（规划方独立确认 40/40 + 4 类契约漏洞）已完成：区分三层校验（存在性→声明类型→阶段语义），补齐 diagnosis.primary_category、confidence 三字段类型+未校准双报、trace/manifest 的 reviewer/reviewed_at、manifest 必填字段类型、array<string> 元素类型、verification_result 字段校验；测试由 40 增至 56。再次等待规划方复核。
  - `cmake_ctest_status = unverified_tool_unavailable`（本机无 CMake）
  - `cross_platform_status = unverified`（仅在本机 MSVC/x64 验证，未做 GCC/Clang 交叉验证）

## 1. 目标与范围

实现一个**纯 C++17** 命令行工具 `hy3_algotrace validate <data_dir>`，加载并校验
`data/manifest.json` 与 `data/problems/*.json`，对照 Phase 1A 数据契约
（schema 0.3.0 / taxonomy 1.0.0）做**数据结构与契约一致性**校验。

**严格范围（只做契约校验）**：
- 校验文件结构、JSON 解析、版本一致性、ID / 外键、诊断规则、manifest 汇总计数；
- **不**调用模型 API、**不**连接外部 OJ、**不**执行任何候选代码；
- **不**实现 `Hy3Client` / `ProcessEvaluator` / `CandidateRunner`（这些是 Phase 2+ 的内容）。

## 2. 依赖策略

- 唯一第三方依赖：`nlohmann/json` **v3.12.0** 单头文件。
- 供应商锁定（vendored）于 `third_party/nlohmann/json.hpp`，从官方 URL 下载：
  - 头文件：`https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp`
  - 许可证：`https://raw.githubusercontent.com/nlohmann/json/v3.12.0/LICENSE.MIT`
- **SHA-256 校验通过**：`aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`
  （953436 字节），与官方发布校验值一致。
- 原因：离线可用、版本固定、可复现；不引入 FetchContent / 网络 / Python。许可证 MIT，已随包保留 `LICENSE.MIT` 与 `README.md`（说明来源与用途）。

## 3. 模块结构

```
include/hy3_algotrace/
  diagnostic.hpp     # Diagnostic 结构 + 稳定错误码 errc::E_* + formatDiagnostic()
  json_loader.hpp    # LoadResult + loadJsonFile()（仅做读取/解析，无业务规则）
  validator.hpp      # ValidationSummary + validateDataset() + sortDiagnostics()
src/
  json_loader.cpp    # 以二进制读取文件 → nlohmann::json::parse；失败返回 E_FILE_READ/E_JSON_PARSE
  validator.cpp      # 全部可执行规则（A–G）；收集尽量多的诊断，不早退
  main.cpp           # CLI：validate / --help；退出码 0/1/2
tests/
  validator_tests.cpp  # 依赖自由的 16 项正/负向测试 + 真实数据集（argv[1] 或 ../data 或 data）
third_party/nlohmann/  # 供应商锁定的 json.hpp + LICENSE.MIT + README.md
build-msvc/            # 本地 MSVC 编译产物（git 忽略，非 CMake 产出）
```

依赖方向：`CLI(main) → DatasetValidator/Diagnostic → JsonLoader → nlohmann/json`。
无循环依赖；校验器不反向依赖 CLI。

## 4. 校验规则（A–G，可执行化自 data-contract.md / error-taxonomy.md）

- **A. 必需顶层键**：每题文件缺 `meta/problem/reference_verdict/test_cases/reasoning_traces/candidate_solutions/diagnoses/verification_results` → `E_MISSING_KEY`（允许额外键）。
- **B. 版本与题目一致性**：`meta.schema/taxonomy/dataset_version` 须与 manifest 一致（`E_VERSION_MISMATCH`）；`problem.id` 须与文件名 `<id>.json` 匹配（`E_PROBLEM_ID_FILE_MISMATCH`）；`reference_verdict.problem_id` 须等于 `problem.id`（`E_BAD_PROBLEM_FK`）。
- **C. 推理轨迹**：`id` 唯一（`E_DUPLICATE_ID`，**跨文件**全局唯一）；`problem_id` 外键（`E_BAD_PROBLEM_FK`）；固定值 `trace_origin=="model_generated"`、`generator_model=="hy3"`、`annotator=="hy3_draft"`（`E_INVALID_ENUM`）；`review_status` 枚举（`E_INVALID_ENUM`）且语义——`planner_reviewed` 要求 `reviewer`/`reviewed_at` 非空，否则 `E_REVIEW_STATUS_SEMANTIC`；`pending_planner_review` 要求二者为 null；`steps` 为必需数组（`E_MISSING_KEY`），`steps[].stage` 枚举（`E_INVALID_ENUM`），`steps[].text` 必需且非空（`E_MISSING_KEY`/`E_TYPE_MISMATCH`）。
- **D. 候选解法**：`id` 唯一（`E_DUPLICATE_ID`，**跨文件**全局唯一）；`trace_id` 外键（`E_BAD_TRACE_FK`，基于全局索引）；`language=="cpp"`、`standard=="c++17"`（固定值，`E_INVALID_ENUM`）；`source_code` 必需（`E_MISSING_KEY`）；`execution_status` 枚举（`E_INVALID_ENUM`）；含 `implementation_consistency` 步骤的轨迹必须有对应候选解法，否则 `E_IMPLEMENTATION_WITHOUT_SOLUTION`。
- **E. 测试用例**：`id` 唯一（`E_DUPLICATE_ID`，**跨文件**全局唯一）；`problem_id` 外键（`E_BAD_TEST_FK`）；`input` 必需且非空（`E_MISSING_KEY`/`E_TYPE_MISMATCH`）；`expected_output` 必需；`origin`/`purpose` 枚举（`E_INVALID_ENUM`）。
- **F. 运行验证结果（Phase 1A 不变量）**：`verification_results` 必须为空，否则 `E_UNEXPECTED_VERIFICATION_RESULT`，并校验其 `solution_id`/`test_id`/`verdict` 外键与枚举（`E_BAD_SOLUTION_FK`/`E_BAD_TEST_FK`/`E_INVALID_ENUM`）。
- **G. 诊断**：`trace_id` 外键（`E_BAD_TRACE_FK`）；每轨迹恰好 1 条 diagnosis（`E_DIAGNOSIS_CARDINALITY`）；`status` 枚举（`E_INVALID_ENUM`）；`correct` 要求 `primary_category=null` 且 `findings` 为空，否则 `E_CORRECT_WITH_FINDINGS`；`incorrect` 要求 `findings` 非空且 `primary_category` 非空且出现在某 `finding.category`，否则 `E_INCORRECT_WITHOUT_FINDINGS` / `E_PRIMARY_NOT_IN_FINDINGS`；`undetermined` 要求 `primary_category=null`，否则 `E_STATUS_PRIMARY_MISMATCH`；`findings[].stage/category` 枚举（`E_INVALID_ENUM`）；`confidence` 在 Phase 4 前必须为 null，否则 `E_UNCALIBRATED_CONFIDENCE`。
- **H. manifest 汇总重算**：`category_counts` 的 7 类、`status_counts` 的 correct/incorrect/undetermined、`test_origin_counts` 的 4 种 origin —— **每个合法枚举键都必须存在且为整数**；缺失键 → `E_MISSING_KEY`，非整数（如字符串 `"0"`）→ `E_TYPE_MISMATCH`，值与重算不同 → `E_MANIFEST_COUNT_MISMATCH`。键存在且值为 `0` 合法。`problem_count/trace_count/problem_ids` 同样重算比对；`review_status` 与所有 trace 的 `review_status` 一致；`planner_reviewed` 要求 `reviewer`/`reviewed_at` 非空。
- **I. 必填子字段与固定值（修复轮 + R2 新增）**：`meta` 必需 `schema_version`/`taxonomy_version`/`dataset_version`/`source_reference`/`created_at`；`problem` 必需 `source`(固定 `codeforces`)/`title`/`statement`/`constraints`/`reference_tags`/`algorithm_type`(固定 `greedy`，非 greedy 如 `"dp"` → `E_INVALID_ENUM`)；`reference_verdict` 必需 6 个字段 + `problem_id` FK；`reasoning_trace` 必需 `author`/`trace_origin`/`generator_model`/`annotator`(固定值)/`review_status`/`steps`；`step` 必需 `stage`/`text`/`relies_on`；`candidate_solution` 必需 `id`/`trace_id`/`language`/`standard`/`source_code`/`execution_status`，其中 `execution_status` 对 phase1a-pilot-001 数据集**固定为 `not_run`**（其它枚举值 `passed`/`failed`/`error` 保留为通用契约的未来状态，但在此数据集被 `E_INVALID_ENUM` 拒绝）；`diagnosis` 必需 `id`/`trace_id`/`status`/`findings`(数组)，`primary_category` 必填且须为 `string|null`（缺键 → `E_MISSING_KEY`，类型错 → `E_TYPE_MISMATCH`，非空时须为 7 类枚举且 incorrect 时须出现在 findings），`confidence`/`confidence_method`/`calibration_version` **三者都必须存在**（`confidence` 声明类型 number\|null，`confidence_method`/`calibration_version` 声明类型 string\|null），且 Phase 4 校准前必须为 null（类型错 → `E_TYPE_MISMATCH`，非 null → `E_UNCALIBRATED_CONFIDENCE`，两者同时报告）；`finding` 必需 `stage`/`category`/`locating`/`evidence`/`suggestion`；`reasoning_trace` 的 `reviewer`/`reviewed_at` 必填且 `string|null`；`verification_result` 非空时必填各字段（solution_id/test_id/actual_output/verdict/runtime_ms 必填，finding_ref 可选 string\|null）。`notes`/`intended_outcome` 等契约明确标为可选的字段不强制，存在时须为 string。校验严格按三层顺序：存在性 → 声明类型 → 阶段语义，不得把缺键静默当 null。
- **J. category_counts 去重语义（修复轮新增）**：`category_counts` 表示「涉及该类别的轨迹数」，按轨迹去重——每条 diagnosis/trace 先收集 `set<category>`，再向全局计数累加；同一条轨迹即使有两条相同 category 的 finding，该类别也只贡献 1。
- **K. 两阶段校验（R2 新增，消除文件顺序依赖）**：阶段 1 `loadProblem` 加载每个问题文件、读取结构、建立数据集级全局 ID/ownership/reference 索引（不解析跨文件外键）；阶段 2 `resolveForeignKeys` 在**完整索引**上解析所有外键、关联关系、基数与语义规则。`candidate_solution.trace_id` / `diagnosis.trace_id` / `verification_result.solution_id`/`test_id` 的解析不再依赖目标文件先读还是后读；`implementation_consistency` 与 `candidate_solution` 的关联也使用数据集级 `solTraces` 集合。真实坏外键仍报原稳定错误码（`E_BAD_TRACE_FK`/`E_BAD_SOLUTION_FK`/`E_BAD_TEST_FK`）。

稳定错误码：`E_USAGE E_DATA_DIR_NOT_FOUND E_MANIFEST_NOT_FOUND E_PROBLEMS_DIR_NOT_FOUND E_FILE_READ E_JSON_PARSE E_MISSING_KEY E_TYPE_MISMATCH E_VERSION_MISMATCH E_DUPLICATE_ID E_BAD_PROBLEM_FK E_BAD_TRACE_FK E_BAD_SOLUTION_FK E_BAD_TEST_FK E_DIAGNOSIS_CARDINALITY E_INVALID_ENUM E_CORRECT_WITH_FINDINGS E_INCORRECT_WITHOUT_FINDINGS E_PRIMARY_NOT_IN_FINDINGS E_IMPLEMENTATION_WITHOUT_SOLUTION E_MANIFEST_COUNT_MISMATCH E_UNCALIBRATED_CONFIDENCE E_UNEXPECTED_VERIFICATION_RESULT`，另含补充码 `E_PROBLEM_ID_FILE_MISMATCH E_REVIEW_STATUS_SEMANTIC E_STATUS_PRIMARY_MISMATCH`。完整映射见 `docs/data-contract.md` 附录 A。

## 5. CLI 行为

- `hy3_algotrace validate <data_dir>`：校验数据集；按确定性顺序打印诊断与汇总报告；退出 `0`=PASS，`1`=FAIL。
- `hy3_algotrace --help | help`：打印用法，退出 `0`。
- 其它调用：打印 `E_USAGE` 与用法，退出 `2`。
- 允许访问文件范围严格限定在 `<data_dir>`（manifest + problems/*.json），不读取其它路径。

## 6. 测试矩阵（33 项 + 真实数据）

| # | 用例 | 期望 |
| --- | --- | --- |
| 1 | 真实 Phase 1A 数据 `validate data` | PASS（out 为空） |
| 2 | 损坏 JSON | `E_JSON_PARSE` |
| 3 | 缺顶层键 | `E_MISSING_KEY` |
| 4 | 单文件重复 trace.id | `E_DUPLICATE_ID` |
| 5 | diagnosis.trace_id 无法解析 | `E_BAD_TRACE_FK` |
| 6 | 单轨迹 2 条 diagnosis | `E_DIAGNOSIS_CARDINALITY` |
| 7 | correct 带 findings | `E_CORRECT_WITH_FINDINGS` |
| 8 | incorrect 无 findings | `E_INCORRECT_WITHOUT_FINDINGS` |
| 9 | primary 不在 findings | `E_PRIMARY_NOT_IN_FINDINGS` |
| 10 | implementation 步骤无解法 | `E_IMPLEMENTATION_WITHOUT_SOLUTION` |
| 11 | 非法 status 枚举 | `E_INVALID_ENUM` |
| 12 | manifest 计数被篡改 | `E_MANIFEST_COUNT_MISMATCH` |
| 13 | confidence 非 null | `E_UNCALIBRATED_CONFIDENCE` |
| 14 | 伪造 verification_result | `E_UNEXPECTED_VERIFICATION_RESULT` |
| 15 | 合法零计数（generated:0/undetermined:0） | 无假阳性 PASS |
| 16 | 轨迹含解法 + 实现步骤 | 无假阳性 PASS |
| 17 | **跨文件**重复 trace.id | `E_DUPLICATE_ID` |
| 18 | **跨文件**重复 diagnosis.id | `E_DUPLICATE_ID` |
| 19 | **跨文件**重复 test_case.id | `E_DUPLICATE_ID` |
| 20 | **跨文件**重复 candidate_solution.id | `E_DUPLICATE_ID` |
| 21 | algorithm_type="dp" | `E_INVALID_ENUM` |
| 22 | trace 缺 steps | `E_MISSING_KEY` |
| 23 | test_case 缺 input | `E_MISSING_KEY` |
| 24 | candidate_solution 缺 source_code | `E_MISSING_KEY` |
| 25 | step 缺 text | `E_MISSING_KEY` |
| 26 | finding 缺 evidence | `E_MISSING_KEY` |
| 27 | meta 缺 schema_version | `E_MISSING_KEY` |
| 28 | steps 不是 array（嵌套类型错） | `E_TYPE_MISMATCH` |
| 29 | manifest 缺 test_origin_counts.generated | `E_MISSING_KEY` |
| 30 | manifest 缺 status_counts.undetermined | `E_MISSING_KEY` |
| 31 | manifest 零计数写成字符串 | `E_TYPE_MISMATCH` |
| 32 | 同轨迹两条同 category finding → 计数去重为 1 | PASS（无假阳性） |
| 33 | 真实数据（显式路径传入，等同 CTest） | PASS（无 skip） |
| 34 | 跨文件**向后**外键引用（目标后读） | PASS（顺序无关） |
| 35 | 跨文件外键引用（调换引用/目标顺序） | PASS（顺序无关） |
| 36 | candidate_solution.execution_status=passed | `E_INVALID_ENUM`（pilot 数据集仅接受 not_run） |
| 37 | diagnosis 缺 confidence_method | `E_MISSING_KEY` |
| 38 | diagnosis 缺 calibration_version | `E_MISSING_KEY` |
| 39 | confidence_method 非 null | `E_UNCALIBRATED_CONFIDENCE` |
| 40 | calibration_version 非 null | `E_UNCALIBRATED_CONFIDENCE` |

测试对真实数据集与全部负向用例均只断言**稳定错误码**，不依赖自由文本。共 **40 项**。

## 7. 本地功能验证（MSVC，规划方授权）

- **编译器**：Microsoft (R) C/C++ Optimizing Compiler **Version 19.51.36248** for x64
  （Visual Studio 18 Community，MSVC 14.51.36231）。
- **环境初始化**：`"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"`。
- **编译命令（CLI）**：
  ```
  cl /nologo /std:c++17 /EHsc /utf-8 /W4 ^
     /external:I third_party /external:W0 ^
     /I include /I third_party ^
     /Fo:build-msvc\ /Febuild-msvc\hy3_algotrace.exe ^
     src\main.cpp src\json_loader.cpp src\validator.cpp
  ```
- **编译命令（测试）**：同上，源换为 `tests\validator_tests.cpp`（其余相同），输出 `build-msvc\validator_tests.exe`。
- 说明：因 nlohmann/json.hpp 体积大且 `/W4` 下会产生海量第三方告警，故用 `/external:I third_party /external:W0` 将供应商头标记为外部、抑制其告警，仅对自有代码保持 `/W4`。
- **结果**：
  - CLI 编译退出码 `CLI_EXIT=0`；测试编译退出码 `TESTS_EXIT=0`。
  - **警告数量：0**（自有代码 + 校验器在 `/W4` 下零告警）。
  - 测试运行：`validator_tests: 33 passed, 0 failed`，退出 `0`。
  - `validate data`：退出 `0`，打印 `result: PASS`（problems=3, traces=9, diagnoses=9, tests=9, candidate_solutions=1, verification_results=0）。
  - `--help`：退出 `0`；非法参数：退出 `2`。
- **数据不可变校验**：对 4 个数据文件（`manifest.json`、3 个题目文件）计算 SHA-256，
  与 Phase 1A 基线**完全一致**（见第 9 节），确认 Phase 1B 未改动任何数据记录。
- 编译产物仅写入 `build-msvc/`，并已在 `.gitignore` 中忽略；源码目录（`src/`、`tests/`、`include/`）无散落 `.obj`/`.exe`。

## 8. 实现过程中修复的问题

1. **校验器 `correct` 分支逻辑反了**：原 `if (!findingsOk || findings.empty() || !primary.empty())` 会在「correct 且 findings 为空」这一**合法**状态下误报 `E_CORRECT_WITH_FINDINGS`，从而令真实数据与全部合法数据集 FAIL。改为 `!findings.empty()`。
2. **`checkManifestSummary` 的 `const Accum& acc`**：内部用 `acc.catCounts[cat]` 等 `map::operator[]`（非 const），编译报错 C2678。改为 `Accum& acc`。
3. **测试嵌套括号字面量歧义**：`reasoning_traces` 的三层嵌套 `json::array({json::object({...})})` 触发语法错误。改写为显式分步构造（`json trace = json::object(); ...; p["reasoning_traces"] = json::array({trace});`），消除歧义。
4. **`loadJsonFile` 绝对路径拼接缺陷**：当传入已是绝对路径的路径且 `baseDir="."` 时，会被错误前缀为 `./C:\...`。修复：若路径 `fs::path::is_absolute()` 则忽略 `baseDir` 直接使用。

## 8b. 规划方独立审查修复轮（5 个漏报）

修复轮由规划方在 MSVC 通过编译 + 构造损坏数据后确认存在漏报触发；本机仍仅 MSVC 验证，未装 CMake，未改正式数据，未提交/推送。

### 发现的 5 个漏报与修复方法

1. **全数据集 ID 唯一性（跨文件漏报）**：原 `traceIds/solIds/tcIds/dgIds` 仅为 `validateProblem` 内部局部集合，只能发现单文件内重复；跨问题文件重复会 PASS。
   - 修复：将 5 个 ID 命名空间（`problem.id`/`reasoning_trace.id`/`diagnosis.id`/`candidate_solution.id`/`test_case.id`）提升为 `Accum` 的**数据集级全局集合**，在 `validateDataset` 跨文件累积；重复即 `E_DUPLICATE_ID`。外键解析（`traceToProblem`/`solToTrace`/`diagToTrace`/`testToProblem` 全局 map）同样基于全局索引，不假设引用目标与引用者同文件。
   - 新增测试：17/18/19/20 覆盖四类跨文件重复 ID。
2. **必填子字段缺失漏报**：原仅检查 8 个顶层键，多实体内部缺字段仍 PASS。
   - 修复：按 `docs/data-contract.md` 字段表为 `meta/problem/reference_verdict/test_case/reasoning_trace/step/candidate_solution/diagnosis/finding/verification_result/manifest` 建立必填字段 + 类型校验；修复三处已确认漏报——`algorithm_type="dp"`→`E_INVALID_ENUM`、`trace` 缺 `steps`→`E_MISSING_KEY`、`test_case` 缺 `input`→`E_MISSING_KEY`。固定值校验：`source=codeforces`、`algorithm_type=greedy`、`trace_origin=model_generated`、`generator_model=hy3`、`annotator=hy3_draft`、`language=cpp`、`standard=c++17`、`confidence=null`（契约明确标为可选的 `notes`/`intended_outcome` 等不强制）。
   - 新增测试：21/22/23/24/25/26/27/28。
3. **manifest 零计数字段漏报**：原把「键缺失」当成 0，删除 `status_counts.undetermined` / `test_origin_counts.generated` 仍 PASS。
   - 修复：`checkSummaryInt` 区分三态——键缺失→`E_MISSING_KEY`，非整数→`E_TYPE_MISMATCH`，值与重算不同→`E_MANIFEST_COUNT_MISMATCH`；键存在且值为 0 合法。对 `category_counts`(7)、`status_counts`(3)、`test_origin_counts`(4) 全部合法枚举键强制存在性与整数类型。
   - 新增测试：29/30/31（删键 / 删键 / 字符串零）。
4. **category_counts 统计语义错误**：原按 finding 计数，同轨迹两条同 category finding 会要求计数为 2。
   - 修复：每条 diagnosis/trace 先收集 `set<category>` 再去重累加，每类别按轨迹贡献 1。
   - 新增测试：32（同轨迹重复 category → manifest 计数 1 PASS）。
5. **CMake 结构不满足要求**：原 CMake 把相同源分别编进两个 executable，无可复用 core library，且用了空格未验证的 generator expression。
   - 修复：改为 `add_library(hy3_algotrace_core STATIC ...)`（含 `json_loader.cpp`/`validator.cpp`+headers），CLI 与 tests 均 `target_link_libraries(... hy3_algotrace_core)`；用清晰 `if(MSVC)/else()` 分别加 `/W4 /EHsc /utf-8` 与 `-Wall -Wextra -Wpedantic`；`third_party` 作为 `SYSTEM` include 抑制第三方告警；`add_test` 显式传入 `"${CMAKE_CURRENT_SOURCE_DIR}/data"`，避免 `../data`/CWD 猜测导致真实数据测试被跳过。
   - 新增测试：33（真实数据显式路径，等同 CTest，不得 skip）。

### 重新构建与测试结果（MSVC）

- 编译器版本、编译命令、退出码、零警告、CLI/测试运行与首轮一致（见第 7 节）。
- 测试总数 **33**，通过 **33**，失败 **0**；`validate data` 退出 `0` / `result: PASS`；`--help` 退出 `0`；非法参数退出 `2`。
- 数据哈希与 Phase 1A 基线逐字节一致；`git diff -- data` 无输出。

### CMake / 跨平台 / 提交状态

- `cmake_ctest_status = unverified_tool_unavailable`（本机无 CMake，未执行）。
- `cross_platform_status = unverified`（仅本机 MSVC/x64）。
- 未提交 / 未打 tag / 未推送 / 未进入 Phase 2；目标分支 `main`。

## 8c. Phase 1B-R2 最终收口修订（规划方复现的 3 类问题 + CMake + 文档）

限 Phase 1B 内，不进入 Phase 2；未改 Phase 1A 数据内容，未提交/推送，未装 CMake，未连 OJ，未执行候选解法，未用 Python。

### 修复内容

1. **CMake 依赖传播**：原 `third_party` 仅作为 core 的 `PRIVATE` include，消费者/测试链接 core 时无法继承 `<nlohmann/json.hpp>` 搜索路径。改为 `include/` 保持 `PUBLIC`、`third_party` 作为 `SYSTEM PUBLIC` 挂到 `hy3_algotrace_core`（`target_include_directories(... SYSTEM PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party>)`），删除重复的 `PRIVATE third_party` 配置。CLI/tests 仍只链接 core，不复制源文件。未声称本地运行过 CMake。
2. **消除全局外键的文件顺序依赖**：原实现在单文件处理时立即解析外键，虽 ID 集合已提升为全局，但 `candidate_solution.trace_id`/`diagnosis.trace_id`/`verification_result` 外键与 `implementation_consistency↔candidate_solution` 关联仍按文件遍历顺序结算，导致「在较早读取的文件里引用较晚读取文件的 trace」会被误报 `E_BAD_TRACE_FK`。改为明确两阶段：`loadProblem`（阶段 1）加载全部文件、读取结构、建立全局 ID/ownership/reference 索引；`resolveForeignKeys`（阶段 2）在**完整索引**上解析所有外键、关联、基数与语义规则。真实坏外键仍报 `E_BAD_TRACE_FK`/`E_BAD_SOLUTION_FK`/`E_BAD_TEST_FK`。
3. **补齐 Phase 1A 固定值与必填字段**：
   - `candidate_solution.execution_status` 对 phase1a-pilot-001 **固定为 `not_run`**；`passed`/`failed`/`error` 保留为通用契约未来状态，但在此数据集被 `E_INVALID_ENUM` 拒绝（同步文档规则 I 与测试 36）。
   - `diagnosis` 的 `confidence`/`confidence_method`/`calibration_version` **三者都必须存在**，且 Phase 4 校准前必须为 `null`：缺键 → `E_MISSING_KEY`，类型错 → `E_TYPE_MISMATCH`，非 null → `E_UNCALIBRATED_CONFIDENCE`（测试 37/38/39/40）。
4. **修正文档矛盾（data-contract §2 / §2.1）**：统一为状态转换语义——模型生成时 `review_status=pending_planner_review`（reviewer/reviewed_at=null），规划方复核后转 `planner_reviewed`（非空），当前 Phase 1A 基线已是 `planner_reviewed` 且不等同 human/expert-reviewed；移除「Phase 1A 固定 pending_planner_review」「必须保持 pending_planner_review」等过期固定表述。同步 README/roadmap 状态行（测试数 33→40）。

### 重新构建与验证（MSVC）

- 编译器 **Version 19.51.36248** for x64；命令见第 7 节（`/std:c++17 /EHsc /utf-8 /W4` + `/external:I third_party /external:W0`）。
- **自有代码警告数：0**（修复了 `validator.cpp` 中一处空受控语句 `C4390`）。
- 测试总数 **40**，通过 **40**，失败 **0**；`validate data` 退出 `0`/`result: PASS`；`--help` 退出 `0`；非法参数退出 `2`。
- 数据哈希与 Phase 1A 基线逐字节一致；`git diff -- data` 无输出。
- 新增测试：34/35 跨文件外键顺序无关；36 execution_status=passed；37/38 缺 confidence_method/calibration_version；39/40 confidence_method/calibration_version 非 null。

### CMake / 跨平台 / 提交状态（R2 仍不变）

- `cmake_ctest_status = unverified_tool_unavailable`（本机无 CMake，未执行）。
- `cross_platform_status = unverified`（仅本机 MSVC/x64）。
- 未提交 / 未打 tag / 未推送 / 未进入 Phase 2；目标分支 `main`。状态：`phase1b_r2_functionally_verified_with_msvc_pending_planner_review`。

## 8d. Phase 1B-R2.1 schema conformance 最终微修（规划方确认 4 类契约漏洞）

限 Phase 1B 内，不扩展业务功能、不改两阶段架构、不进入 Phase 2；未改 Phase 1A 数据内容，未提交/推送，未装 CMake，未连 OJ，未执行候选解法，未用 Python。

### 修复内容（区分三层校验：① 存在性 ② 声明类型 ③ 阶段语义）

1. **diagnosis.primary_category**：原为「缺键/类型错被 `nonEmptyString` 静默当 null」，导致 correct 诊断删掉该键仍 PASS。改为 `reqNullableString`（存在性→`E_MISSING_KEY`，类型 string\|null→否则 `E_TYPE_MISMATCH`），并在阶段 2 补枚举校验（非 7 类→`E_INVALID_ENUM`，incorrect 时须非空且出现在 findings）。新增 helper：`reqNullableString` / `reqNullableNumber` / `reqStringArray` / `optString`。
2. **diagnosis confidence 三字段类型 + 未校准双报**：原类型检查逻辑有 bug（字符串被放行，漏 `E_TYPE_MISMATCH`）。改为按声明类型分别校验——`confidence`：number\|null；`confidence_method`/`calibration_version`：string\|null；非 null 时**同时**报告 `E_TYPE_MISMATCH`（若类型也错）与 `E_UNCALIBRATED_CONFIDENCE`。
3. **trace / manifest 的 reviewer / reviewed_at**：原为 `nonEmptyString` 静默读取，缺键/数字/布尔/数组/对象均不报错。改为 `reqNullableString`（必填、string\|null），并在 review_status 语义层校验（`planner_reviewed` 须非空，`pending_planner_review` 须 null，否则 `E_REVIEW_STATUS_SEMANTIC`）。manifest 另补 `review_status` 枚举校验（`E_INVALID_ENUM`）。
4. **manifest 必填字段真正的类型校验**：`schema_version` 等原仅做 `E_MISSING_KEY` 且用 `nonEmptyString` 读取（数字被当空，summary 打印空 schema）。改为 `reqString`/`reqInt`/`reqArray`/`reqObject` 真实类型校验——`schema_version=3`（数字）→ `E_TYPE_MISMATCH`，不再以空字符串作为合法 schema。`kManifestRequiredKeys` 新增 `reviewer`/`reviewed_at`。
5. **array\<string> 元素类型**：`problem.reference_tags` / `reference_verdict.expected_boundaries` / `steps[].relies_on` / `manifest.problem_ids` 用 `reqStringArray` 逐个校验元素为 string，非 string 元素 → `E_TYPE_MISMATCH`（不再只用外层 `reqArray` 静默读取）。
6. **verification_result 字段校验**：Phase 1A 不变量仍先报 `E_UNEXPECTED_VERIFICATION_RESULT`，但对其中记录仍完成字段检查（`solution_id`/`test_id`/`actual_output`/`verdict`/`runtime_ms` 必填，`verdict` 枚举，`finding_ref` 可选 string\|null）；缺键 → `E_MISSING_KEY`，类型错 → `E_TYPE_MISMATCH`。
7. **可选字段收口**：`problem.notes` / `reasoning_trace.intended_outcome` / `test_case.notes` 仅当契约明确标记可选才允许缺失，存在时须为 string（`optString`）；其他未明确标记可选的字段一律不得当可选。

### 重新构建与验证（MSVC）

- 编译器 **Version 19.51.36248** for x64；`/std:c++17 /EHsc /utf-8 /W4` + `/external:I third_party /external:W0`。
- **自有代码警告数：0**（修复了 `validator.cpp` 中一处未使用局部变量 `C4189`）。
- 测试总数 **56**，通过 **56**，失败 **0**；`validate data` 退出 `0`/`result: PASS`；`--help` 退出 `0`；非法参数退出 `2`。
- 数据哈希与 Phase 1A 基线逐字节一致；`git diff -- data` 无输出。
- 新增测试：41 缺 primary_category；42 primary_category 非法枚举；43 manifest.schema_version 数字；44/45/46 confidence/confidence_method/calibration_version 类型错+未校准双报；47/48 trace 缺 reviewer/reviewed_at；49 trace reviewer 数字；50 manifest 缺 reviewer；51 manifest.review_status 非法；52/53/54 reference_tags/relies_on/problem_ids 非 string 元素；55 verification_result 缺 actual_output/runtime_ms；56 合法原始数据仍 PASS。

### CMake / 跨平台 / 提交状态（R2.1 仍不变）

- `cmake_ctest_status = unverified_tool_unavailable`（本机无 CMake，未执行）。
- `cross_platform_status = unverified`（仅本机 MSVC/x64）。
- 未提交 / 未打 tag / 未推送 / 未进入 Phase 2；目标分支 `main`。状态：`phase1b_r2_1_schema_verified_with_msvc_pending_planner_review`。
- *（注：上述 `unverified` 为 R2.1 当时本机状态；后续已由 GitHub CI 在 Windows/Linux 实际验证推翻，见 §11。）*

## 10. Phase 1B 发布前 CI 准备（2026-08-24）

### 10.1 规划方验收记录

- **Phase 1B-R2.1 已于 2026-08-24 通过 `codex_planner` 技术验收**：schema 0.3.0 三层校验（存在性 → 声明类型 → 阶段语义）、稳定错误码复用、56 项测试（含双错误码断言）均被确认达到契约收口目标。
- **本地 MSVC 功能验证通过**：56/56 测试通过、自有代码 `/W4` 0 警告、CLI 对 `data/` 输出 PASS、4 个数据文件 SHA-256 与 Phase 1A 基线逐字节一致。
- **canonical CMake/CTest 与跨平台状态仍等待 GitHub CI**：本机无 CMake，未能执行 `cmake -S . -B build` → `ctest` → `run_cli` 的 canonical 流程；此状态须由 `ubuntu-latest` + `windows-latest` 双矩阵 CI 补齐。
- **不等同于人工/专家审查**：`codex_planner` 技术验收与 MSVC 功能验证仅代表机器侧契约与构建层面的确认，不构成人工或领域专家对算法标注质量的审查。

### 10.2 本轮操作（仅 CI 准备，不进入 Phase 2）

- 清理仓库根目录未跟踪文件 `nul`（Windows 保留设备名，经 `\\?\` 命名空间强制删除；不递归、不触碰其他未跟踪交付物如 `include/`、`third_party/`、`src/json_loader.cpp` 等）。
- 新增 `.github/workflows/ci.yml`：触发 `push→main` / `pull_request` / `workflow_dispatch`；`permissions: contents: read`；matrix `ubuntu-latest` + `windows-latest`；`actions/checkout@v4`；不下载项目依赖（nlohmann/json 已 vendored），不使用 Python；canonical 流程覆盖 core library、CLI、tests 构建、56 测试、run_cli 对 data 输出 PASS。
- 未修改 `data/`；未 commit / tag / push；未进入 Phase 2。
- 当前阶段状态：`phase1b_planner_reviewed_pending_github_ci`。

## 9. 数据不可变校验（基线对照）

| 文件 | SHA-256（当前 = 基线） |
| --- | --- |
| `data/manifest.json` | `c7ebf9a62fd734f7ed648552e8e6ded4b1294204c85853638661f467f9cd5a06` |
| `data/problems/cf_160A.json` | `538ab2601e53d786ba2433912e6dee6846a2adc87241d778aef592375dd17ec6` |
| `data/problems/cf_545D.json` | `040abf23efcefeb31191e676e8e90494cdf0899ce3ac655b54b52a1e6ef56886` |
| `data/problems/cf_1398B.json` | `358badb47f3609fddc2a823ec58bcbe563b4a25011b2942bcd20a66ab7fa5490` |

以上 4 个哈希与 Phase 1A 冻结基线逐字节一致，确认本阶段仅新增代码与文档，未修改任何数据记录。

## 10. 未实现 / 未验证项

- **CMake / CTest 未实际运行**：本机无 CMake（含 VS 自带目录亦无）。`CMakeLists.txt` 已按 C++17 + core 库 + CLI + 测试 + `enable_testing()` 写好（canonical、跨平台），但**未执行**，亦未声称通过。标记 `cmake_ctest_status = unverified_tool_unavailable`。
- **跨平台未验证**：仅在本机 MSVC/x64 验证；GCC/Clang 与 Linux/macOS 未测，标记 `cross_platform_status = unverified`。
- **未提交 / 未打 tag / 未推送 / 未进入 Phase 2**：依规划方补充授权，本地仅完成功能验证；后续由规划方推送 GitHub，并在具备 CMake 的环境或 GitHub CI 中补做 canonical CMake/CTest 验证。

## 11. 下一步

1. 规划方独立复核通过后，将代码推送 GitHub（目标分支 `main`）。
2. 在具备 CMake 的环境 / GitHub CI 中执行 `cmake -S . -B build && cmake --build build && ctest --test-dir build`，补做 canonical 验证。
3. 验证通过后进入 **Phase 2**（C++17 基础评测管线：Ingest → ProcessEvaluator → Reporter 最小管线）。

> 注：§11 为早期规划视角；实际推进见 §12——Phase 1B 已于 2026-08-24 完成实现提交、推送与 GitHub CI 验证。

## 12. Phase 1B 最终验收与 CI 验证记录（2026-08-24）

### 12.1 规划方独立确认

- **规划方**：`codex_planner`（技术验收，非人工 human_reviewed、非专家 expert-reviewed）。
- **实现 commit**：`8145b4f1b894101a8cbb1a302c6028b4fe8b3a01`（已成功推送至 `origin/main`）。
- **GitHub Actions workflow**：`CI`（`.github/workflows/ci.yml`）。
- **run ID**：`32656643095`；**event**：`push`；**conclusion**：`success`。
- **Windows job（`windows-latest`）**：Configure / Build / CTest / Run CLI 全部 `success`。
- **Ubuntu job（`ubuntu-latest`）**：Configure / Build / CTest / Run CLI 全部 `success`。
- 两个平台均完成 canonical CMake configure → build → CTest（56 项测试）→ CLI `validate data` 输出 PASS。
- CI 链接：<https://github.com/Smily2333/hy3-algotrace/actions/runs/32656643095>

### 12.2 统一最终状态

- `cmake_ctest_status = verified_github_ci`（canonical CMake/CTest 已由真实 GitHub CI 在 Windows/Linux 验证；本地机器无 CMake，本地未运行，但不再总体标记为 unverified）。
- `cross_platform_status = verified_windows_linux`（Windows + Linux 已验证；**macOS 未验证，不得声称 macOS 已验证**）。
- Phase 1B 最终状态：**`phase1b_complete_ci_verified`**。

### 12.3 边界与声明

- `codex_planner` 技术验收仅代表规划方技术层面的契约与构建确认，**不等同于人工（human_reviewed）或专家（expert-reviewed）审查背书**。
- 本地 MSVC 56/56 测试保持通过；4 个数据文件 SHA-256 与 Phase 1A 基线逐字节一致（data 未改动）。
- **未进入 Phase 2**；**未创建 tag**（等待 GitHub CI 绿灯后由规划方决定）。
- 后续进入 Phase 2 须由规划方另行授权与启动。
