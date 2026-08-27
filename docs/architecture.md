# 系统架构（规划稿）

> **2026-08-27 执行入口更新：** 本文件保留 Phase 历史架构和已落地模块说明；其中阶段编号、未来模块与下一步安排不构成当前执行指令。当前范围、M1–M4 定义和验收统一见 [当前执行路线图](roadmap.md)，目标架构见 [项目方案](project-proposal-2026-08-27.md)。

> 本文描述**未来**系统的模块与流程。Phase 0 仅冻结设计，不实现任何模块。
> 相关约定：数据字段见 `data-contract.md`；错误类别见 `error-taxonomy.md`；阶段划分见 `roadmap.md`。

## 1. 总体职责划分

系统按三条相互独立的职责线组织，避免把「判断算法类型」「评估推理过程」「验证代码 / 测试」混在同一处：

| 职责 | 名称 | 输入 | 输出 | Phase 0 状态 |
| --- | --- | --- | --- | --- |
| 算法类型识别 | `algorithm_type_identification` | 题面 + 约束 | 类型标签（greedy / search / dp / graph …） | 初版人工标注 `greedy` |
| 过程评估 | `process_evaluation` | 推理轨迹 + 题目 | 诊断（status / primary_category / findings） | 设计冻结 |
| 代码 / 测试验证 | `code_test_verification` | C++17 解法 + 测试信息 | 一致性 / 正确性信号（test_case, verification_result） | 仅规划 |

> 初版（Phase 0–3）所有样本人工标注为 `greedy`，由 `process_evaluation` 直接处理；自动路由在 **Phase 6** 引入。

## 2. 主要模块

- **Ingest（接入）**：载入题目记录与推理轨迹（格式见 `data-contract.md`）。本阶段不实现解析器。
- **TypeRouter（类型路由）**：初版为人工标注透传；Phase 6 起自动识别算法类型并分发。
- **ProcessEvaluator（过程评估器）**：核心。对每条推理轨迹，按六环节逐步评估，产出诊断（status / primary_category / findings，结构见 `data-contract.md`）。
- **CodeVerifier（代码验证器）**：将候选 C++17 解法与思路比对（实现一致性），并基于 `test_case` / `verification_result`（见 `data-contract.md`）评估正确性信号。Phase 2+ 落地。
- **Scorer（评分校准）**：将过程评估结果映射为分数 / 置信度，Phase 4 校准（校准前 `confidence` 为 null）。
- **Reporter（报告）**：汇总为诊断报告（见 README 输出形式）。

## 3. 处理流程

```
题目记录 ─┐
          ├─► Ingest ─► TypeRouter ─(greedy)─► ProcessEvaluator ─┐
候选推理轨迹 ─┘                                            │                   │
                                                            │                   ▼
候选 C++17 解法 ─────────────────────────────► CodeVerifier ┘            Scorer ─► Reporter ─► 诊断报告
```

文字描述：

1. `Ingest` 载入题目、推理轨迹与（可选）候选解法、测试信息。
2. `TypeRouter` 确定算法类型；初版直接采用人工 `greedy` 标签。
3. `ProcessEvaluator` 按六环节评估推理轨迹，调用错误分类体系（见 error-taxonomy）给出定位，并产出诊断（status / primary_category / findings）。
4. 若提供候选解法与测试信息，`CodeVerifier` 评估「实现与思路一致性」及运行验证结果，补充正确性信号。
5. `Scorer` 汇总（Phase 4 前 `confidence` 为 null），`Reporter` 输出诊断报告。

## 4. 六环节评估维度（固定顺序）

过程评估固定按以下顺序拆解（与错误分类一一对应）：

| 顺序 | stage key | 中文 | 说明 |
| --- | --- | --- | --- |
| 1 | `problem_understanding` | 题意理解 | 是否准确理解题面、约束与目标 |
| 2 | `greedy_choice` | 贪心策略选择 | 所选贪心量 / 排序 / 优先级是否正确 |
| 3 | `greedy_proof` | 贪心性质 / 交换论证 | 正确性论证是否成立 |
| 4 | `complexity` | 复杂度分析 | 时间 / 空间复杂度是否正确 |
| 5 | `boundary` | 边界条件 | 特殊 / 极端输入是否考虑 |
| 6 | `implementation_consistency` | 实现与思路一致性 | 代码与文字思路是否一致 |

## 5. 设计约束

- 类型识别、过程评估、代码验证三者解耦，可独立演进。
- 初版不实现自动路由、不调用模型 API、不连接外部 OJ。
- 所有模块接口在 Phase 1–2 依据 `data-contract.md` 落地。
- 诊断结论区分 `status`（correct / incorrect / undetermined）与 finding 类别（见 error-taxonomy）；`undetermined` 表示输入不足无法判定，**不作为已确认错误**参与计数。
- `implementation_consistency` 环节仅在轨迹关联候选 C++17 解法时才评估；无候选解法时该环节应**省略**，不得凭空断言「思路与实现一致」（详见 `data-contract.md` 第 2 节）。缺少可选代码**不会**使纯思路轨迹自动判错。
- 数据集采用**一题一文件**布局：`data/problems/<id>.json` 顶层含 `meta / problem / reference_verdict / test_cases[] / reasoning_traces[] / candidate_solutions[] / diagnoses[] / verification_results[]`，由 `data/manifest.json` 汇总（详见 `data-contract.md`）。Phase 1A 尚未执行候选代码，所有 `verification_results` 为空数组。

## 6. Phase 1B 已落地子集：契约校验器（DatasetValidator）

Phase 1B 实现了**规划中未来系统的子集**——只做「数据结构与契约一致性校验」，不eval推理过程、不执行代码。

### 6.1 已落地模块

| 模块 | 文件 | 职责 | 状态 |
| --- | --- | --- | --- |
| `Diagnostic` | `include/hy3_algotrace/diagnostic.hpp` | `Diagnostic` 结构 + 稳定错误码 `errc::E_*` + `formatDiagnostic()` | 已实现 |
| `JsonLoader` | `src/json_loader.cpp` + `json_loader.hpp` | 二进制读取文件 → `nlohmann::json::parse`；失败返回 `E_FILE_READ`/`E_JSON_PARSE`，**无业务规则** | 已实现 |
| `DatasetValidator` | `src/validator.cpp` + `validator.hpp` | 加载 manifest + 各题文件，执行全部 executable 规则（A–H），收集尽量多诊断，产出 `ValidationSummary` | 已实现 |
| `CLI` | `src/main.cpp` | `validate <data_dir>` / `--help`；退出码 0/1/2 | 已实现 |
| `validator_tests` | `tests/validator_tests.cpp` | 56 项正/负向测试 + 真实数据集；仅断言稳定错误码 | 已实现 |

### 6.2 依赖方向（无循环）

```
CLI(main) → DatasetValidator / Diagnostic → JsonLoader → nlohmann/json
```

- `Diagnostic` 是叶子（被其余模块依赖，不自依赖业务逻辑）。
- `JsonLoader` 只负责「读 + 解析」，把失败转成结构化错误码；不识别任何契约语义。
- `DatasetValidator` 依赖 `JsonLoader` 与 `Diagnostic`，是唯一的契约规则承载者。
- `CLI` 依赖 `DatasetValidator`，负责参数解析与退出码，**不**包含校验逻辑。

### 6.3 明确未实现（属 Phase 2+）

Phase 1B **不**包含以下模块（与第 1–2 节规划一致）：

- `Hy3Client` / 模型 API 调用；
- `ProcessEvaluator`（六环节推理过程评估、错误定位、评分）；
- `CodeVerifier` / `CandidateRunner`（候选代码的本地受限编译、超时运行与输出对比；外部 OJ 对接不在 Phase 2D 初版范围内）；
- `Scorer` / `Reporter`（置信度校准、诊断报告渲染）。

校验器对 `candidate_solutions` / `verification_results` 只做**结构一致性**检查
（外键、枚举、`verification_results` 在 Phase 1A 必须为空的不变式），**不会**编译或运行任何代码。

## 6.4 Phase 2B 已落地子集：离线评估管线（导出 → 导入 → 报告）

Phase 2B 在 Phase 1B 契约校验器之上，实现了**完整离线评估管线的三段式 C++ 工具**：导出 Prompt、导入模型原始响应并生成 prediction wrapper、汇总指标生成报告。**全程不调用模型 API、不连接 OJ、不执行候选代码**（这些属 Phase 2C/2D 离线交互或本地受限验证，不在初版自动范围）。

### 6.4.1 已落地模块

| 模块 | 文件 | 职责 | 状态 |
| --- | --- | --- | --- |
| `Sha256` | `include/hy3_algotrace/sha256.hpp` + `src/sha256.cpp` | 自包含 FIPS 180-4 SHA-256（NIST 标准向量验证）+ UTF-8 规范化（CRLF/CR→LF、剥离 BOM、拒绝无效 UTF-8 / 嵌入 NUL） | 已实现 |
| `PromptExporter` | `include/hy3_algotrace/prompt_exporter.hpp` + `src/prompt_exporter.cpp` | `extractTemplateBody`、`projectTraceInput`（显式 allowlist 投影）、`auditStructuralLeakage`、`renderPrompt`（5 占位符）、`exportPrompts`（临时目录+原子发布、run-manifest） | 已实现 |
| `PredictionImporter` | `include/hy3_algotrace/prediction_importer.hpp` + `src/prediction_importer.cpp` | `saveRawResponse`（逐字节 + raw hash）、`loadPromptSha`、`classifyResponse`（6 态严格判别、无 fence/repair）、`writePredictionWrapper`、`importResponse`、`markNotAttempted`（显式，不推断） | 已实现 |
| `Reporter` | `include/hy3_algotrace/reporter.hpp` + `src/reporter.cpp` | `loadGoldDiagnosis`、`buildReport`（严格按 `docs/phase-02-metrics.md`：parse/status/primary/micro/macro/pair + 混淆矩阵 + 去重 + 零分母 + N/A 区分）、`writeReport`、`generateReport`（仅当 run 完整时更新 `completed_at`） | 已实现 |
| `CLI` | `src/main.cpp` | `export-prompts` / `import-response` / `mark-not-attempted` / `report` / `validate` / `--help`；退出码 0/1/2 | 已实现 |
| `prompt_exporter_tests` | `tests/prompt_exporter_tests.cpp` | 22 项测试（SHA 向量、CRLF/BOM、UTF-8/NUL、模板边界、占位符、allowlist、leakage 非误报、null 候选、cf_160A_t3 关联、9 轨迹字典序、确定性、拒绝覆写、unsafe-id、真实数据集成、validator 回归） | 已实现 |
| `prediction_importer_tests` | `tests/prediction_importer_tests.cpp` | 空/空白、非 JSON、Markdown fenced、尾随文本、缺键/错类型/额外键/非法枚举、confidence 非 null、correct/incorrect/undetermined 语义、primary 不在 findings、implementation_consistency 无候选拒绝、trace_id 不一致、raw 字节哈希、拒绝覆写、not_attempted、确定性、无 sentinel | 已实现 |
| `reporter_tests` | `tests/reporter_tests.cpp` | 完美预测、部分解析失败、缺 wrapper→incomplete、同 category 去重、completed_at 仅在完整时更新、report.json/md 一致 | 已实现 |
| `phase2b_e2e_tests` | `tests/phase2b_e2e_tests.cpp` | 端到端 synthetic smoke：真实数据集导出 9 prompt → 导入合成响应 → 显式 mark 其余 → 报告；断言无 gold 泄漏、report 确定性数值、completed_at 更新；fixtures 标记 `SYNTHETIC_TEST_FIXTURE` | 已实现 |

### 6.4.2 依赖方向（无循环，叠加于 Phase 1B）

```

CLI(main) → PromptExporter / PredictionImporter / Reporter / Sha256
          → DatasetValidator / Diagnostic → JsonLoader → nlohmann/json
```

- `Sha256`、`Diagnostic`、`JsonLoader` 是叶子。
- `PromptExporter` 依赖 `JsonLoader` + `Sha256` + `Diagnostic`，不依赖 `DatasetValidator`。
- `PredictionImporter` 依赖 `JsonLoader` + `Sha256`，**读取 prompt 但不读 gold**；仅做 schema/语义校验与 wrapper 生成。
- `Reporter` 是**唯一**读取 gold（`data/problems/*/diagnoses`）的模块；gold 只在内存比较与 `report.json`/`report.md` 中出现，**绝不**写回 prediction 文件。
- `CLI` 负责参数解析与退出码，不含评估逻辑。

### 6.4.3 明确未实现（属 Phase 2C/2D+）

Phase 2B **不**包含以下模块：

- 模型 API 自动调用（`ProcessEvaluator` 的在线推理循环）；本阶段只有离线 `export` / `import` / `report`。
- `CandidateRunner` / `CodeVerifier`（候选代码编译、运行、OJ 对比，属 Phase 2D 本地受限版）。
- 指标中的 `confidence` 校准（Phase 4 前固定为 null，不参与任何指标）。
- `hallucination_flag_rate` 的自动判定（需人工/规则检查器，本阶段只记录定性信号）。

### 6.4.4 失败安全与审计特性

- `exportPrompts` 拒绝覆写已存在的 `run_dir`；写临时目录后原子 `rename`。
- `importResponse` 拒绝覆写已存在的 raw / prediction（`E_RAW_EXISTS` / `E_PREDICTION_EXISTS`）；raw 响应**逐字节**保存，`raw_response_sha256` 按原始字节计算，不做规范化/修复；**绝不**静默剥离 Markdown 围栏/前言/尾随文本，也**绝不** repair JSON——无法解析即 `invalid_json`。
- `classifyResponse` 严格 6 态判别；`parse_status != parsed` 时 `prediction` 必为 null；内部 `__parse_failed__` sentinel 只用于 `Reporter` 内存指标，**绝不**写回任何文件。
- `Reporter` 在 wrapper 缺失时标记 `run_complete=false`，**绝不**默认 `correct`；`completed_at` 仅在 run 完整且传入合法 ISO-8601 时写回 `run-manifest.json`。
- gold 隔离：prediction wrapper 结构由协议 §7 固定，无任何 `diagnoses` 字段；`Reporter` 比较后 gold 只进入 `report.*`，不污染 `predictions/`。

### 6.4.5 Phase 2C ModelClient 最小垂直层

Phase 2C 在不改变冻结 prediction schema 的前提下增加 transport-neutral 模型层：

| 模块 | 职责 |
| --- | --- |
| `IModelClient` / `FakeModelClient` | 接收已渲染且已哈希的 Prompt；返回原始模型内容字节、模型身份、调用状态与时间元数据；fake 仅用于零费用测试 |
| `ModelRunner` | `loadPromptSha → IModelClient::invoke → importResponseBytes`；只在传输成功时进入 Importer，timeout/auth/rate-limit/provider/transport 失败绝不伪装成 `model_call_not_attempted` |
| `Hy3ModelClient` | 按腾讯云 TokenHub OpenAI-compatible Chat 协议构造非流式 `hy3` 请求，解析 `choices[0].message.content`；Bearer Key 由显式配置或 `TOKENHUB_API_KEY` 注入且不得进入诊断 |

`Hy3ModelClient` 依赖注入式 `IHttpTransport`。生产实现由 Windows 系统 WinHTTP / Linux 系统 libcurl 提供统一语义：TLS 校验、官方 origin pin、禁重定向和重试、显式 connect/total timeout；CI 只做无网测试。`runRecordedModelForTrace` 在发送前原子创建一次性 `model-calls` 侧车并在导入后原子完成，任何既存侧车均阻止重调。成功返回的空文本、非法 JSON 或 schema/语义错误仍由 `PredictionImporter` 分别判为既有 `empty_response` / `invalid_json` / `schema_invalid` / `semantic_invalid`，adapter 不修复模型内容。run manifest 是模型身份的权威来源，Runner 与 wrapper 会校验/继承该身份。

网络依赖不 vendoring：Windows 使用随受信任 runner/操作系统交付和维护的 WinHTTP（Microsoft 系统组件）；Ubuntu CI 从 Ubuntu 官方签名 APT 仓库安装 `libcurl4-openssl-dev`（curl license），安装步骤输出精确包版本，包完整性由 APT 仓库签名与 runner 镜像信任链验证。生产部署必须以同等方式记录实际系统组件/包版本。

### 6.4.6 独立交互诊断 Demo

交互 Demo 不复用冻结数据集投影或 gold-aware Reporter，而是在同一模型基础层之上建立独立契约：

```text
Browser (no key)
  → loopback cpp-httplib server
  → InteractiveDiagnosisRequest + interactive prompt/strict validator
  → ModelRunner::invokeModelOnce
  → Hy3ModelClient → ProductionHttpTransport → TokenHub
```

| 模块 | 职责 |
| --- | --- |
| `InteractiveDiagnosis` | 严格解析临时用户输入、渲染并哈希独立 Prompt、一次性目录/sidecar latch、保存 raw、校验独立响应 schema、只返回脱敏诊断 |
| `InteractiveHttpApplication` | `health` / `diagnose` 的无 socket 可测业务面；限制 body/content type 与并发调用，区分 transport 和 diagnosis 错误 |
| `InteractiveServer` / `hy3_algotrace_demo` | 仅绑定 `127.0.0.1`，校验 Host，提供静态页面与 API；Key 只存在于 C++ 服务进程 |
| `web/` | 原生中文输入/结果界面；七类中文映射、六阶段 findings、loading/失败/复制 JSON；不保存 Key，不刷新重调 |

`runRecordedModelForTrace` 仍专属于带 manifest/PredictionImporter 的冻结评测运行；交互路径只共享其 transport-neutral `invokeModelOnce` 单次调用边界，避免把临时输入伪装成 dataset trace。交互产物写入 Git 忽略目录，绝不回写 `data/` 或进入 Reporter 指标。可选 C++17 只作为模型文本上下文，不被编译或执行。

本地 server 使用固定 `cpp-httplib v0.51.0`（MIT）单头文件；它只承载 loopback HTTP，TokenHub HTTPS 继续由现有 WinHTTP/libcurl transport 负责。

## 6.5 冻结文件边界

以下文件在 Phase 2B 中**只读不写**（实现不得修改）：`data/`（数据契约 0.3.0 逐字节不变）、`prompts/hy3-evaluator-v1.md`（冻结 Prompt 模板）、`docs/phase-02-protocol.md`、`docs/phase-02-metrics.md`。任何指标/枚举/语义变更必须回到规划方修订这些冻结文件，而非在 C++ 中自行创造类别。

## 7. 模型适配边界（offline/manual + 可注入 adapter）

offline/manual 仍是无需凭证的正式回退路径；Phase 2C 同时提供 transport-neutral adapter，使模型传输与严格解析、gold 比较和报告计算保持解耦。

```text
Ingest
  → PromptExporter        （显式 allowlist 构造输入 JSON，渲染 BEGIN/END 间模板，写入 experiments/.../prompts/<trace_id>.txt；仅对输入 payload 做 structural leakage 递归 key 检查）
  → [offline/manual] 外部 Hy3 推理后导入 raw response
    或 [adapter] ModelRunner → IModelClient（Fake / Hy3 TokenHub）
  → PredictionImporter    （保存 raw response，用标准 JSON 解析器 [nlohmann/json] 解析，schema + 语义校验，产出 prediction wrapper）
  → ProcessEvaluator / Comparator（与 gold diagnosis 比较）
  → Reporter              （汇总指标，输出 report.json / report.md）
```

设计约束：

- offline/manual 路径不调用 API；只有显式 `call-hy3` 命令进入生产 transport，并受一次性 sidecar、官方 HTTPS origin、环境凭证和无重试策略约束。
- PromptExporter 的 leakage 检查仅针对**输入 payload**（替换占位符后的输入 JSON 块），**不**扫描模板任务说明与输出 schema 中的 `status` / `findings` / 7 类 category 名称等通用字符串（它们在输出契约中合法）。structural leakage（禁止 key 进入输入）由递归 key 检查拦截；semantic leakage（自由文本直接透露 gold label）通过排除 `test_cases.notes` 等字段降低，其余自由文本须单独人工/规则审计。
- Phase 2A 只冻结协议、Prompt 模板、指标与运行目录；`PromptExporter` / `PredictionImporter` / `Reporter` 的实现属 **Phase 2B**，`CandidateRunner` 属 **Phase 2D**（Phase 2D 初版仅本地受限编译/运行/对比，不连接外部 OJ，见 `roadmap.md`）。
- prompt 与 raw response 的原始文本必须独立保留（见 `docs/phase-02-protocol.md` 第 10、9 节），不得只保存清洗后 JSON；SHA-256 用于关联与复现（模板哈希与实例哈希分别定义，见协议第 9 节）。
