# 独立贪心评测契约 v2（2026-08-28）

最新状态：离线验证后，用户授权的三次v2开发调用已完成，**2 parsed/1 schema_invalid**；两份代码通过隔离固定测试。累计6次、76496 token，六次开发上限已用完。详见[真实报告](journal/m3-development-v2.md)，不宣称Hy3总体质量改善。网页协议及旧Phase2评测不变。下文离线验证与调用前预算数为历史检查记录，不是当前余额。

## 改动与边界

- 新模板 [hy3-greedy-evaluation-v2.md](../prompts/hy3-greedy-evaluation-v2.md) 独立渲染，不拼接交互模板。唯一顶层为 `schema_version: greedy-evaluation-v2`、`diagnosis`、`solution_code`。
- diagnosis继续使用 `interactive-diagnosis-v2` 的原分类、代码优先语义、证据和LF行范围校验；不放宽校验，不补字段、不移动响应字段、不修复JSON或剥fence。
- 模板内有完整合法JSON输入/输出，离线测试将两者与 [合成fixture](../evaluation/fixtures/evaluation-v2-example.json) 比较，并调用生产校验器。示例不是模型质量证据，也不执行示例代码。
- `parse` 返回 `expected_schema_version` 和 `validation_errors: [{path,message}]`，路径采用JSON Pointer。例如 `/diagnosis/steps/1/id`、`/diagnosis/steps/0/code_location/snippet`；返回首个失败，跨字段语义错误可能定位到 `/diagnosis`，不是穷举全部错误。诊断消息不回显模型正文。
- `export-v2/import-v2/report-v2/call-v2` 明确选择新版本；无后缀命令保留v1。错版本拒绝，新报告必须带v2记录身份，失败也不能混入旧记录。统计规则不变，报告附 `evaluation_version`，报告结构版本仍为 `greedy-report-v1`。
- 新调用元数据记录模板ID、LF规范化模板hash、实际Prompt/data hash；安全保存allowlist的finish_reason（stop/length/content_filter/tool_calls/function_call），其它值为null。cached/reasoning token仅作可选用量细分，不能重复加入总token。无完整HTTP头；缺失字段不猜测。

旧三次记录没有finish_reason，不能追补或从token量反推。模板歧义是改进点，**不是已证实的失败根因**。原模板、原始失败及hash保持不变；见 [原试跑更正](journal/m3-development-pilot.md)。

## 离线命令

以下从仓库根目录执行，Windows Release路径如下；Linux使用对应build可执行文件路径。输出目录/文件必须不存在；不要覆盖旧产物。

```powershell
.\build\m1\Release\hy3_evaluate.exe export-v2 evaluation/materials/dataset.json build/evaluation-v2-export
.\build\m1\Release\hy3_evaluate.exe import-v2 evaluation/materials/dataset.json s001 RAW_FILE NEW_RECORD_FILE
.\build\m1\Release\hy3_evaluate.exe report-v2 evaluation/materials/dataset.json V2_RECORD_BUNDLE NEW_REPORT_FILE
```

RAW_FILE等是命令参数占位，不能原样执行。bundle与v1一样为 `{data_kind, records}`，data_kind只允许synthetic或real；离线Fake必须synthetic。报告不会自动把未执行代码标为答案通过，人工复核也不伪造。保留集导出不等于保留集调用或正式冻结。

## 调用前门槛记录（已执行完三次，不得再次自动调用）

如决定进行少量新版本开发验证，可用call-v2，仍只允许s001/s002/s003，各新版本最多一次；这不是覆盖旧失败或自动重试。

- call与call-v2都必须使用原campaign `build/m3-development-20260827` 的budget账本；新目录拒绝，不因更换输出契约或切回旧命令重置预算。此调用入口绑定当前开发campaign，不是通用实验调度器。
- 原账本已有3次、36420 token，剩余263580 token/35次总上限；新版验证最多再3次，累计最多6次开发请求。预算上限仍300000 token/38次，每次保守预留214016 token，余量不足提前停止，不保证能用完3次。
- 新attempt名为v2-s001等，样本ID仍为s001等；旧s001目录、reserve/done及stop-expansion原样保留。halt或未知账目不绕过；基础设施失败不重发。
- 需要原账户确认与正常凭证加载流程；本轮没有读取真实Key、模型调用或候选代码执行。记录输出不公开raw/requestID等私有材料。
- 旧正式纳入仍0；v2先验证可用性，不能把开发结果混为保留集或正式指标。人工仅确认三条gold，余22条仍待审；独立答案验证、正式冻结和最终交付门槛不变。

## 验证记录

本地Windows完整构建成功；CTest **12/12**，其中evaluation_tests **56/56**（合成数据）。实际命令：

```powershell
.\build\_tools\cmake-4.3.4-windows-x86_64\bin\cmake.exe --build build/m1 --config Release --parallel 4
.\build\_tools\cmake-4.3.4-windows-x86_64\bin\ctest.exe --test-dir build/m1 -C Release --output-on-failure
.\build\m1\Release\evaluation_tests.exe .
```

离线CLI检查：导出25条v2 Prompt；用原始响应重放旧版import，三条仍schema_invalid、raw SHA全部不变；v2合成失败报告成功，混入v1记录被拒绝；错误campaign目录在构造模型客户端前被拒绝，无网络请求。原账本仍3次/36420 token。

覆盖完整示例、版本隔离、缺失/错层字段路径、三态语义、CRLF/空行/重复片段、无JSON修复、finish_reason白名单和用量细分。没有编译/运行候选或示例代码，未加载真实Key。

本次代码检查点 `c72590cacc8e7d7e6d9f50fa0f68feb1623c7fbd`，
[CI 33091674872](https://github.com/Smily2333/hy3-algotrace/actions/runs/33091674872)：Windows/Ubuntu configure、build、全部CTest、CLI数据校验均成功。后续仅补写本段CI证据，不重复触发CI。旧冻结数据、旧Prompt、原Phase2协议指标、pilot及evaluation/materials/results均零diff；工作区未新增Python、真实raw或凭证。新版模型效果仍未验证。

## 正式批次预算与冻结门禁（2026-09-09 新增）

正式入口与开发入口隔离，但复用 `build/m3-development-20260827/budget` 的同一追加式账本。正式集合提案见 [`evaluation/formal-20260909`](../evaluation/formal-20260909/freeze-manifest.proposed.json)：9 条、三档难度各3条，包含 s024/s037 两条固定测试答案通过但过程错误的样本。它在项目作者确认前保持 `pending_human_review`；`formal-plan` 和 `formal-call` 都会拒绝它。

逐请求上界为 `UTF-8(serialized prompt) 字节数 + max_tokens(13312) + 固定 envelope 余量(1024)`。字节数保守覆盖项目提供文本的输入 token；额外 1024 覆盖单条 user message 的 API 固定封装。TokenHub [OpenAI Chat Completions 协议](https://cloud.tencent.com/document/product/1823/135872)说明 `max_tokens` 是单次响应最大输出 token，达到时 `finish_reason=length`；`completion_tokens` 已含思维链，`total_tokens=prompt_tokens+completion_tokens`。因此 reasoning 不重复相加。若 usage 缺失、三项不满足加法、实际值超过预留、或留下未完成 reservation，账本保留未知预留并停止后续调用。

冻结前的只读估算命令：

```powershell
.\build\m1\Release\hy3_evaluate.exe formal-estimate evaluation/formal-20260909/freeze-manifest.proposed.json build/m3-development-20260827
```

当前精确估算为新增 216016、历史加最坏合计 292512 token；每条明细在 [`budget-estimate.proposed.json`](../evaluation/formal-20260909/budget-estimate.proposed.json)。这不是调用授权。真人确认导入、manifest 改为 `frozen` 并重算哈希后，才运行 `formal-plan` 持久化唯一 batch plan，再逐条运行 `formal-call`。正式入口只接受 manifest 中样本；每次调用前还检查整个未完成尾批能否承载。已有 reservation/done、未知账目、hash差异、未确认gold或非holdout样本均失败关闭，无自动重试。
