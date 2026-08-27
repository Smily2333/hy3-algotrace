# M1：代码优先交互 v2

本地中文贪心诊断应用：完整题面 + C++ 代码 → 算法概述、静态三态、
逻辑步骤与位置、错误证据、反例候选和完整参考/修正解法。用户思路、测试、说明可选。
本版本不执行代码、不连接 OJ，不把交互结果放入旧 Phase 2 指标。
M1 验收记录见 [journal/m1-interactive-v2.md](journal/m1-interactive-v2.md)。

## 启动

从仓库根目录运行（已有 CMake 和 C++17 工具链）：

~~~powershell
cmake -S . -B build
cmake --build build --config Release --target hy3_algotrace_demo
.\build\Release\hy3_algotrace_demo.exe --host 127.0.0.1 --port 8080
~~~

Linux/单配置生成器通常使用 build/hy3_algotrace_demo。
打开 http://127.0.0.1:8080/。没有 Key 仍可打开页面，但点击分析不会得到模型结果。
需要真实调用时，仅在服务端配置 TOKENHUB_API_KEY；浏览器不读取、不保存凭证。
启动/health 本身不调用模型。每个请求最多一次调用，不自动重试。

默认模板为 prompts/hy3-interactive-diagnosis-v2.md，默认审计根目录为
experiments/interactive/runs/v2/。可用 --prompt-template / --artifacts-root 覆盖；
模板必须带准确 v2 标题且仅有一个输入 marker，v1 模板在启动时拒绝。
不要将本地服务通过转发/反向代理公开。本机服务不是多用户鉴权平台或代码沙箱。

## v2 请求契约

POST /api/diagnose，Content-Type: application/json。只允许：

| 字段 | 规则（UTF-8 字节上限） |
| --- | --- |
| schema_version | 必填，interactive-request-v2 |
| request_id | 必填，1～128 位字母/数字/点/下划线/连字符，不能为 . 或 ..；一次性 ID |
| algorithm_type | 必填，仅 greedy |
| problem_statement | 必填完整题面，60,000 字节；不需要拆标题/I/O/约束 |
| cpp_solution | 必填 C++ 文本，120,000 字节；保留缩进和空行 |
| reasoning | 可选，30,000 字节 |
| user_notes | 可选，10,000 字节 |
| test_cases | 可选，最多 20 项；每项仅 input/expected_output 两个字符串，各 20,000 字节 |

必填字符串拒绝缺失、null、非字符串、空字符串和纯 Unicode 空白。
可选 reasoning/user_notes 的缺失、null、空字符串或纯空白都规范为 null；
非空内容保留原样，不 trim。test_cases 缺失/null/[] 均为空数组，空字符串不合法。
每项测试的 input/expected_output 必须为字符串；允许空字符串（空 stdin/stdout 有意义），不接受 null。
HTTP body 另限 256 KiB，渲染 Prompt 限 300,000 字节；所有检查在模型调用前进行。

所有文本拒绝非法 UTF-8/NUL，去除开头 BOM，CRLF/CR 统一 LF。不会删除代码前导空行、
缩进或末尾空行。行号基于规范化后的 cpp_solution。不同原文经规范化相同则源代码 hash 相同。

旧 v1/未版本化/分栏 problem 请求直接返回 400，不通过占位思路“升级”。
响应中的 v1 schema 也拒绝。旧 Prompt 和历史记录保留，不需要通用多版本框架。

### 最小请求（Synthetic 契约示例，不是模型效果证据）

~~~json
{
  "schema_version": "interactive-request-v2",
  "request_id": "example-v2-001",
  "algorithm_type": "greedy",
  "problem_statement": "输入正整数面值 a，1<=a<=100，只有这一枚硬币。取最少枚数使所取价值严格大于剩余价值，输出枚数。",
  "cpp_solution": "#include <iostream>\nint main() { int a; std::cin >> a; std::cout << 1 << '\\n'; }\n"
}
~~~

## v2 响应与诊断含义

浏览器 envelope 保留 ok/request_id/outcome/parse_status/diagnosis/metadata/error。
仅成功且严格校验通过才 ok=true、parse_status=parsed，并提供 diagnosis；失败 diagnosis=null。
响应 schema 固定为 interactive-diagnosis-v2，不修 JSON、不剥 Markdown fence。
模型响应最大 1,000,000 字节。所有对象拒绝未知字段。

| status | 页面含义 |
| --- | --- |
| correct | 未发现明确错误；不是“已证明正确”，也不是通过测试 |
| incorrect | 发现错误；需要合法 primary_category 和非空 findings |
| undetermined | 无法确定；解释信息不足、超范围或证据不足，不编造完整解法 |

correct/undetermined 必须 primary_category=null、findings=[]、first_error.step_id=null。
limitations 至少一项；页面始终额外标注静态/未执行/非证明，不依赖模型自觉声明。

### 完整 diagnosis 示例（与上方请求配套的预设响应）

下面是 response.diagnosis 的完整形状，不是一次真实 Hy3 结果：

~~~json
{
  "schema_version": "interactive-diagnosis-v2",
  "request_id": "example-v2-001",
  "status": "correct",
  "summary": "静态审查未发现明确错误。",
  "limitations": ["未编译运行，不是形式化正确性证明。"],
  "algorithm_overview": {"origin": "model_code_interpretation", "summary": "读取面值并输出一枚。"},
  "steps": [{
    "id": "s1",
    "summary": "读取唯一面值并输出所需枚数",
    "code_location": {
      "start_line": 2, "end_line": 2,
      "snippet": "int main() { int a; std::cin >> a; std::cout << 1 << '\\n'; }"
    }
  }],
  "first_error": {"step_id": null, "explanation": "未发现可靠错误位置。"},
  "primary_category": null,
  "findings": [],
  "counterexample": {
    "availability": "unavailable", "input": null, "expected_output": null,
    "predicted_candidate_output": null, "candidate_output_basis": null,
    "explanation": "未提出反例。", "provenance": "model_proposed_not_executed"
  },
  "reference_solution": {
    "availability": "provided",
    "strategy": "读取面值，输出 1。",
    "correctness": "取零枚不能严格超过正的剩余价值；取一枚后剩余为零，故一枚必要且足够。",
    "complexity": "时间 O(1)，额外空间 O(1)。",
    "boundaries": "a 为正；a=1 和 a=100 都适用。",
    "unavailable_reason": null, "provenance": "model_generated_unverified"
  }
}
~~~

完整错误 fixture 可查 tests/interactive_v2_fixture.hpp。
每项 finding 必须包含 id、step_id、category、reason、input_evidence、
code_location、location_reason、suggestion。input_evidence 为 source/excerpt，
source 仅限实际输入的 problem_statement/cpp_solution/reasoning/user_notes。
引用代码时 excerpt 必须在该 finding 指定行范围的 snippet 内；不能拿全文其他位置作证据。

### 定位规则

- steps 为按算法逻辑顺序排列的数组（最多 30 项）；每项包含唯一 ID、简短 summary、
  code_location。非 undetermined 至少一步；无法解释时允许 undetermined + []。
- code_location 为 start_line/end_line/snippet；整数、1-based、闭区间，不接受浮点数、
  负数、越界或逆序。末尾一个 LF 是分隔符，不额外生成一行；内部空行是真实行。
- snippet 必须是所报完整行以 LF 连接的精确文本，去掉的仅是范围末尾行分隔 LF。
  不 trim、不使用全局 substring 证明位置；重复片段也必须对应所报行。
- finding 的位置包含于引用步骤范围内；step_id 必须存在。无可靠位置时 step_id 和
  code_location 同为 null，location_reason 必须说明原因；已定位时 location_reason=null。
- first_error.step_id 可为 null，但始终要求 explanation。非 null 必须是已报告 findings
  中逻辑顺序最早的步骤（不是最小行号）；存在未定位 finding 时不能声称已确认首次。
- 位置检查证明“引用对应输入”，不证明步骤解释、首次根因或诊断内容正确。

### 分类映射（仅交互 v2）

| 类别 | v2 使用条件 |
| --- | --- |
| problem_misunderstanding | 对题意目标/约束的明确误解 |
| wrong_greedy_choice | 代码中的贪心选择不成立 |
| complexity_error | 代码复杂度与约束不符 |
| boundary_omission | 边界、严格比较条件等遗漏 |
| invalid_greedy_proof | 必须有实际 reasoning，且 input_evidence.source=reasoning |
| implementation_mismatch | 必须有实际 reasoning 并引用，核对用户思路与代码；两者一致不代表符合题意 |
| code_logic_error | v2 最小新增类别：无法更准确归入上述类别的真实代码逻辑问题 |

missing_greedy_proof 在 v2 中不合法。模型概述的 origin 必须为
model_code_interpretation，不能当作用户 reasoning 或独立正确性证据。
这不是冻结 taxonomy 的新版本；没有自动映射回旧实验指标。CF 160A 比较符问题在无思路
v2 中可记 boundary_omission，不再虚构 implementation_mismatch。

### 反例与完整解法

counterexample 固定 provenance=model_proposed_not_executed。
provided 仅用于 incorrect：input/expected_output 为完整字符串，
predicted_candidate_output 可为 null；非 null 时 basis 必须 static_inference。
unavailable 时输入/两种输出/basis 全为 null，explanation 非空。页面明确标为未执行。

reference_solution 固定 provenance=model_generated_unverified。
provided 时 strategy/correctness/complexity/boundaries 四节必须非空，
unavailable_reason=null；unavailable 时四节全 null，原因非空。
undetermined 只能 unavailable。此处输出面向用户的完整解法说明，不是模型内部思维链；
可运行答案形式及独立动态校验是 M2 的待冻结任务，M1 不实施。

## 模板、审计与安全

- 默认模板 ID：hy3-interactive-diagnosis-v2。
- LF 规范化模板 SHA-256：a58623d7543e717f49ebf681044506736ff0e86acfea8c27b79e66dcd32d6d93。
- 默认真实模型仍为 hy3，model_version 不可得时 null，不伪造置信度/校准值。
- metadata/sidecar 同步 request/response schema、模板 ID/hash、Prompt hash、
  source_code_sha256、code_location_basis=lf_1_based_inclusive。
- one-shot 审计目录原子创建于模型调用前。任何既有同名目录拒绝重调。
  内含 prompt.txt、model-call.json、成功传输时的原样 raw-response.txt、
  校验通过时的 diagnosis.json。schema_version=interactive-model-call-v2。
- 浏览器不获得 raw、headers、Key、provider 错误原文或本地路径；textContent 安全渲染。
  保留 HTTP body 限制、CSP、Host allowlist、loopback、单 active call、无自动重试。
- 题面、代码、注释和用户说明均视为数据，不能覆盖 Prompt 指令。提示注入和自然语言判断
  无法仅靠 schema 完全解决；仍需后续真实实验和独立审查。

## Fake 网页检查（零费用）

构建 interactive_server_tests，然后从仓库根目录运行：

~~~powershell
.\build\Release\interactive_server_tests.exe --serve-fake . experiments/interactive/runs/v2/manual-fake 8091
~~~

打开 http://127.0.0.1:8091/。这不是生产参数；测试进程只使用 FakeModelClient，
不构造真实 transport、不读取 Key、不执行代码。顶部会显示 Mock/Fake。

每次成功通过输入校验的提交按固定顺序循环预设场景（**不分析输入**）：

1. 点击“填入示例”，不填思路，分析：边界错误，s3/第 16 行。
2. 将示例停止条件 >= 改为 >，分析：未发现明确错误。
3. 将题面改为“选一些硬币。目标与约束尚未说明。”，分析：无法确定，无完整解法。
4. 重新填入示例并分析：非法 JSON，显示失败。
5. 再手动分析：预设 timeout，显示失败，输入保留。
6. 再手动分析：含 HTML 标签的模型文本，必须只显示文字，不产生图像/脚本。

使用同一 fixture 顺序才能复现；任意输入可能与预设定位不符而被严格校验拒绝，这不是模型能力测试。
每条 Fake 请求仍保留真实业务层的审计和重复保护。Ctrl+C 停止测试服务。
