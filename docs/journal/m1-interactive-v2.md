# M1：交互 v2 本地技术验收

- 日期：2026-08-27。
- 执行依据：当前 `docs/roadmap.md` 第 4 节；仅 M1，不启动 M2。
- 状态：`m1_complete_local_verified_pending_model_evaluation`。
- 分支：`codex/interactive-diagnosis-ui`。
- 开始与结束 HEAD：`8d04513a855c923fa3bf4aebd489d2f2fefca48a`；开始工作区干净，结束为本轮未提交修改。
- 本轮未创建 commit、未 push、未 merge、未发布。没有读取/修改旧 checkout 或父目录 MCP 计划。

## 完成项与修改文件

- `include/hy3_algotrace/interactive_diagnosis.hpp`、`src/interactive_diagnosis.cpp`：代码优先请求、明确版本、Unicode 空白/类型/长度规则、LF 源码 hash、严格响应与位置/步骤引用校验。
- `prompts/hy3-interactive-diagnosis-v2.md`：独立 v2 Prompt，代码与题意为主，明确无思路/证明的适用条件、反例与完整解法不声称验证。
- `src/interactive_demo_main.cpp`、`include/hy3_algotrace/interactive_server.hpp`、`src/interactive_server.cpp`：默认模板和审计目录指向 v2；health 版本/hash 一致；启动拒绝旧模板；测试注入的 Mock 模式明确标识。
- `web/index.html`、`web/app.js`、`web/styles.css`：两框输入、高级选项、三态、模型概述、逻辑步骤/首次错误、代码证据、展开反例与完整解法、复制 JSON/安全文本/提交代码快照。
- `tests/interactive_diagnosis_tests.cpp`、`tests/interactive_server_tests.cpp`、`tests/interactive_v2_fixture.hpp`：复用 CHECK/FakeModelClient，增加输入/响应/定位/调用保护回归及显式 Fake 网页服务。不是新的测试框架或实际模型诊断器。
- `CMakeLists.txt`：两项交互测试显式传入仓库根目录，读取真实 v2 模板。
- `docs/interactive-diagnosis-demo.md`、README、`docs/roadmap.md`、`docs/architecture.md`、本记录：契约、启动、状态与证据同步；历史材料不重写。

模板 LF SHA-256：`a58623d7543e717f49ebf681044506736ff0e86acfea8c27b79e66dcd32d6d93`。
request/response/sidecar 分别为 `interactive-request-v2` / `interactive-diagnosis-v2` /
`interactive-model-call-v2`。旧 v1/未版本化请求拒绝，不虚构思路自动转换。

## 实际构建与测试

本机未发现 CMake，经过正常权限批准，将官方便携 CMake 4.3.4 放入忽略的 `build/_tools/`；没有系统安装或新项目依赖。
包 SHA-256 `86e5fcafb38bdf58346a78b187c7b6b4f252ae5242cffe24c463a92bbd2e77d1` 与 [官方校验清单](https://cmake.org/files/v4.3/cmake-4.3.4-SHA-256.txt) 一致。
MSVC 编译器识别为 `19.51.36248.0`，Windows SDK `10.0.26100.0`。

最初沙箱内 configure 失败：MSBuild MSB6001，重复环境键 Path/PATH。
没有绕过权限或修改系统环境；正常申请后在本机环境运行同一 CMake 流程成功。

下列是实际命令（便于复现，以同一相对路径变量缩写可执行文件）：

```powershell
$m1Cmake = '.\build\_tools\cmake-4.3.4-windows-x86_64\bin\cmake.exe'
$m1Ctest = '.\build\_tools\cmake-4.3.4-windows-x86_64\bin\ctest.exe'
& $m1Cmake -S . -B build/m1 -G 'Visual Studio 18 2026' -A x64
& $m1Cmake --build build/m1 --config Release --target interactive_diagnosis_tests --parallel 4
.\build\m1\Release\interactive_diagnosis_tests.exe .
& $m1Cmake --build build/m1 --config Release --target interactive_diagnosis_tests interactive_server_tests hy3_algotrace_demo --parallel 4
& $m1Ctest --test-dir build/m1 -C Release -R 'interactive_(diagnosis|server)_tests' --output-on-failure
node --check web/app.js
& $m1Cmake --build build/m1 --config Release --parallel 4
& $m1Ctest --test-dir build/m1 -C Release --output-on-failure
.\build\m1\Release\interactive_server_tests.exe .
.\build\m1\Release\hy3_algotrace.exe validate data
.\build\m1\Release\hy3_algotrace_demo.exe --help
.\build\m1\Release\hy3_algotrace_demo.exe --prompt-template prompts/hy3-interactive-diagnosis-v1.md
git diff --check
```

结果：

| 检查 | 本轮实际结果 |
| --- | --- |
| CMake configure / 受影响目标 / 完整 build | 成功 |
| interactive_diagnosis_tests | 108 passed，0 failed |
| interactive_server_tests | 34 passed，0 failed |
| 受影响 CTest | 2/2 通过 |
| 完整 CTest | 11/11 通过（包含旧离线/transport 的无网回归），1.36 秒 |
| JavaScript 语法 | node --check 通过 |
| validate data | PASS：3 题、9 轨迹、9 diagnosis、9 tests、1 candidate、0 verification_results |
| CLI --help | 默认模板/目录显示 v2 |
| 旧模板启动拒绝 | E_TEMPLATE_INVALID，退出码 1，未进入模型配置/调用 |
| 文档 JSON 示例 | 两个示例均可解析，request_id 与第 2 行 snippet 对应 |
| diff 空白检查 | 通过 |

完整 build 有旧 `prompt_exporter_tests.cpp` 与 `phase2b_e2e_tests.cpp` 的 C4189 未用变量警告；没有为本轮扩改这些旧文件，M1 文件没有编译警告。

重点回归：题面/代码最小请求；缺失/null/纯空白/类型/长度/UTF-8；可选项与解析器复用；无思路时禁用缺证明及虚构一致性诊断；正确代码与 CF 160A 比较符、升序错误策略、v2 代码逻辑类别；CRLF/空行/重复片段、行范围/类型、越界和步骤引用；逻辑顺序非最小行号；反例静态推断与完整解法可用性；三态矛盾；v1 拒绝；重复请求、原样 raw、非法 JSON/fence、模型失败与安全错误输出。

## 网页验收：全部为 Mock/Fake

实际启动：

```powershell
.\build\m1\Release\interactive_server_tests.exe --serve-fake . experiments/interactive/runs/v2/m1-browser-fake-20260827 8091
```

使用原生页面与真实 C++ 请求解析、响应校验及审计，仅把 IModelClient 替换为预设 Fake。
浏览器在 `http://127.0.0.1:8091/` 检查，初次 GET 为 200，顶部明确显示 Mock/Fake。
可复现的六场景操作步骤见 [交互文档](../interactive-diagnosis-demo.md#fake-网页检查零费用)。

| 操作 | 实际观察 |
| --- | --- |
| 初次进入 | 仅题面/代码两框，高级选项折叠，无独立标题/I/O/约束必填 |
| 空输入、纯空白代码、120001 字节代码 | 页面明确拒绝，未进入 Fake 调用 |
| 填入示例、不填思路、分析 | 发现错误 / boundary_omission / first_error=s3 / 第 16 行精确片段 |
| 展开反例与完整解法 | 2/5 5 候选反例，候选可能输出标注静态推断；完整策略、正确性理由、复杂度、边界均显示 |
| 复制与 JSON 展开 | “已复制”成功提示可见；展开 JSON 中 v2 schema、s3、第 16 行与后端一致 |
| 将 >= 改成 >，第二次分析 | 未发现明确错误，不显示“已证明正确” |
| 缩减为信息不足题面，第三次分析 | 无法确定；无可靠首次错误、反例不可用、完整解法不可用且说明原因 |
| 第四次非法 JSON | 严格校验失败，不修复、不伪装成功；用户输入保留 |
| 第五次 timeout | 失败提示，无自动重试 |
| 第六次 HTML 字符串 | `<img ... onerror=...>` 作为纯文字；页面 img 数量为 0，无脚本弹窗 |
| 清空 | 两框清空，旧结果/复制按钮隐藏，高级选项折叠 |
| 浏览器日志 | 最终读取 error/warn 列表为空 |

浏览器共触发 **6 次 Fake invocation**：4 次 parsed、1 次 invalid_json、1 次 model_call_failed/timeout。
本地审计 6 个目录；全部 model_name=fake-model、v2 模板及 hash 正确。
截图保存在忽略目录 `build/m1/m1-fake-result.png` 与 `build/m1/m1-fake-full.png`；后者包含展开内容。
原始 Fake 审计仅保留在忽略的实验目录，没有把 raw/运行目录加入 Git。
验收后已停止本轮创建的 8091 Fake 服务，没有停止其他本机服务。

**剪贴板限制：** 页面 clipboard.writeText 返回成功并出现“已复制”，但自动化 clipboard.readText 读到空字符串。
已核对可见 JSON 的完整字段，未据此宣称系统剪贴板字节已独立验证；手动选取 JSON 的后备路径保留。

## 冻结、隐私与范围

`git diff --exit-code` 检查以下集合为零改动：

- `data/`、`prompts/hy3-evaluator-v1.md`、`prompts/hy3-interactive-diagnosis-v1.md`。
- `docs/phase-02-protocol.md`、`docs/phase-02-metrics.md`。
- 旧 Phase 2C/交互 journal 及原 9 条 pilot 结果；原 transport、Key 管理、Reporter 等基础模块。

没有新增 Python 文件。没有读取真实 Key，真实模型调用 **0**，候选代码执行 **0**，OJ 调用 **0**。
Fake 审计中检查不到 Authorization/Bearer/API Key 头或值；源码里的 synthetic-secret 是既有风格的负向测试占位，不是真实凭证。
本轮新增内容无用户本地绝对路径、私人运行正文或凭证。测试只清理自己创建的唯一临时目录。

## 未验证项、风险与停止位置

- 本次未运行 Ubuntu/Linux、macOS 或远端 CI；不以历史 CI 代替当前验证。
- 真实 Hy3 对 v2 的遵循率、错误识别能力、首次根因定位和误报率均未验证；Fake 通过不证明效果提升。
- 位置/引用核对不验证自然语言诊断正确性；模型仍可能误解题目、遗漏错误、过度诊断或受提示注入影响。
- 模型生成反例、参考解法均未执行或独立审查。完整可运行输出的形式和答案校验属于 M2。
- 实际人工抽检：未开展；这是 Planner 的程序技术验收，不是 human_reviewed。
- 系统剪贴板最终字节未独立核验，见上方限制。

M1 必做实现及本地程序验收完成。停止等待 M2 启动授权；不自动开始题集扩展、答案执行器、模型实验或提交/发布。
