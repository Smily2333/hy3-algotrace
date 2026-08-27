"use strict";

const CATEGORY_LABELS = {
  problem_misunderstanding: "题意理解错误", wrong_greedy_choice: "贪心选择错误",
  invalid_greedy_proof: "用户证明无效", complexity_error: "复杂度错误",
  boundary_omission: "边界遗漏", implementation_mismatch: "用户思路与实现不一致",
  code_logic_error: "代码逻辑错误"
};
const STATUS_LABELS = {correct: "未发现明确错误", incorrect: "发现错误", undetermined: "无法确定"};
const SOURCE_LABELS = {problem_statement:"题面", cpp_solution:"代码", reasoning:"用户思路", user_notes:"补充说明"};
const $ = (id) => document.getElementById(id);
const form = $("diagnosisForm");
let calling = false, lastJson = null, submitted = null;
const normalizeLF = (text) => text.replace(/^\uFEFF/u, "").replace(/\r\n?/g, "\n");
const isBlank = (text) => /^[\s\u0085]*$/u.test(text);
const rawValue = (id) => normalizeLF($(id).value);
const optional = (id) => isBlank(rawValue(id)) ? null : rawValue(id);
const byteLength = (text) => new TextEncoder().encode(text).length;

function showView(id) {
  for (const view of ["emptyState","loadingState","errorState","resultContent"]) $(view).hidden = view !== id;
}
function textNode(tag, className, value) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  node.textContent = value == null ? "—" : String(value);
  return node;
}
function setBusy(value) {
  calling = value;
  for (const input of form.querySelectorAll("textarea, button")) input.disabled = value;
  $("submitButton").classList.toggle("loading", value);
  $("submitButton").querySelector(".button-label").textContent = value ? "分析中…" : "分析代码";
}
async function checkHealth() {
  try {
    const response = await fetch("/api/health", {cache:"no-store"});
    const health = await response.json();
    const mock = health.model_mode === "mock_fixture";
    $("mockNotice").hidden = !mock;
    $("healthStatus").className = "health " + (health.ok && (mock || health.tokenhub_status === "configured") ? "ok" : "error");
    $("healthStatus").lastChild.textContent = mock ? "Mock / Fake · 零模型调用" :
      (!health.ok ? "模板不可用" : health.tokenhub_status === "configured" ? "TokenHub 已配置" : "TokenHub 未配置");
  } catch (_) {
    $("healthStatus").className = "health error";
    $("healthStatus").lastChild.textContent = "本地服务未连接";
  }
}
function buildPayload() {
  const fields = [["statement",60000],["cppSolution",120000],["reasoning",30000],
    ["testInput",20000],["expectedOutput",20000],["userNotes",10000]];
  for (const [id, limit] of fields) {
    if (byteLength(rawValue(id)) > limit) throw new Error("输入超出字节上限，请缩短对应字段。");
  }
  if (isBlank(rawValue("statement")) || isBlank(rawValue("cppSolution")))
    throw new Error("完整题面和 C++ 代码均为必填项，不能只有空白。");
  const testInput = rawValue("testInput"), expected = rawValue("expectedOutput");
  const payload = {
    schema_version: "interactive-request-v2",
    request_id: "interactive-v2-" + (globalThis.crypto?.randomUUID ? crypto.randomUUID() :
      Date.now() + "-" + Math.random().toString(16).slice(2)),
    algorithm_type: "greedy", problem_statement: rawValue("statement"),
    cpp_solution: rawValue("cppSolution")
  };
  // Missing optional fields stay absent; no fabricated reasoning or placeholder.
  if (optional("reasoning") !== null) payload.reasoning = optional("reasoning");
  if (optional("userNotes") !== null) payload.user_notes = optional("userNotes");
  if (testInput !== "" || expected !== "") payload.test_cases = [{input:testInput, expected_output:expected}];
  if (byteLength(JSON.stringify(payload)) > 256 * 1024) throw new Error("请求整体超过 256 KiB，请缩短输入。");
  return payload;
}
function showError(kind, code, fallback) {
  const messages = {
    validation_error: ["输入未通过校验", "请检查题面、代码及高级选项的格式和长度。"],
    configuration_error: ["TokenHub 未配置", "请在服务端配置凭证；浏览器不读取或保存 Key。"],
    authentication_error: ["TokenHub 认证失败", "服务端凭证不可用；本次不会自动重试。"],
    rate_limited: ["调用频率受限", "本次不会自动重试。"],
    busy: ["已有分析进行中", "请等待正在进行的分析完成。"],
    timeout: ["等待超时", "尚未获得有效诊断；不能据此判断代码。不会自动重试。"],
    transport_error: ["传输失败", "请检查本地服务或网络。本次不会自动重试。"],
    duplicate_request: ["请求已处理或尝试过", "相同请求 ID 不会再次调用模型。"],
    invalid_model_response: ["模型响应未通过严格校验", "没有可展示的有效诊断；未修复 JSON，也未剥离 Markdown 标记。"]
  };
  const [title, message] = messages[kind] || ["未得到诊断", fallback || "本地服务未能完成分析。"];
  $("errorTitle").textContent = title; $("errorMessage").textContent = message;
  $("errorCode").textContent = code || kind || "unknown";
  lastJson = null; $("copyButton").hidden = true; showView("errorState");
}
function appendLocation(root, location) {
  if (!location) return;
  root.append(textNode("small","location-label","代码第 " + location.start_line +
    (location.end_line === location.start_line ? "" : "–" + location.end_line) + " 行"));
  root.append(textNode("pre","code-snippet",location.snippet));
}
function row(root, title, value, code = false) {
  root.append(textNode("h4","",title), textNode(code ? "pre" : "p",code ? "code-snippet" : "prose",value));
}
function renderResult(body) {
  const d = body.diagnosis;
  if (!body.ok || d?.schema_version !== "interactive-diagnosis-v2" || !STATUS_LABELS[d.status]) {
    showError("invalid_model_response",body.parse_status,"服务端没有返回有效 v2 诊断。"); return;
  }
  $("statusCard").className = "status-card " + d.status;
  $("statusText").textContent = STATUS_LABELS[d.status]; $("statusCode").textContent = d.status + " · 静态审查";
  $("primaryCategory").textContent = d.primary_category ? CATEGORY_LABELS[d.primary_category] : "无";
  $("diagnosisSummary").textContent = d.summary;
  $("limitations").replaceChildren(...d.limitations.map(item => textNode("li","",item)));
  $("algorithmOverview").textContent = d.algorithm_overview.summary;
  $("firstErrorStep").textContent = d.first_error.step_id ? "步骤 " + d.first_error.step_id : "无可靠错误定位";
  $("firstErrorExplanation").textContent = d.first_error.explanation;
  const steps = $("algorithmSteps"); steps.replaceChildren();
  for (const step of d.steps) {
    const item = textNode("li",step.id === d.first_error.step_id ? "first-error" : "","");
    item.append(textNode("strong","",step.id + " · " + step.summary));
    appendLocation(item,step.code_location); steps.append(item);
  }
  if (!d.steps.length) steps.append(textNode("li","hint","信息不足，未生成可靠的算法步骤。"));
  const findings = $("findingsGroups"); findings.replaceChildren();
  $("findingCount").textContent = d.findings.length + " 项";
  for (const finding of d.findings) {
    const card = textNode("article","finding-card","");
    card.append(textNode("span","finding-category",CATEGORY_LABELS[finding.category]));
    row(card,"原因 · " + finding.id + " / " + (finding.step_id || "未定位"),finding.reason);
    row(card,"输入证据 · " + SOURCE_LABELS[finding.input_evidence.source],finding.input_evidence.excerpt,true);
    appendLocation(card,finding.code_location);
    if (!finding.code_location) row(card,"定位限制",finding.location_reason);
    row(card,"修正建议",finding.suggestion); findings.append(card);
  }
  if (!d.findings.length) findings.append(textNode("p","hint","未报告明确错误；不等于证明代码正确。"));
  const counter = $("counterexampleContent"); counter.replaceChildren();
  const c = d.counterexample;
  if (c.availability === "provided") {
    row(counter,"输入（反例候选）",c.input,true);
    row(counter,"按题意应得的输出（模型推导，未验证）",c.expected_output,true);
    if (c.predicted_candidate_output !== null) row(counter,"候选代码可能输出（静态推断，未执行）",c.predicted_candidate_output,true);
  }
  row(counter,c.availability === "provided" ? "反例说明" : "未提供反例",c.explanation);
  const solution = $("solutionContent"); solution.replaceChildren();
  const s = d.reference_solution;
  if (s.availability === "provided") {
    for (const [key,label] of [["strategy","完整策略"],["correctness","正确性理由"],["complexity","时间与空间复杂度"],["boundaries","关键边界"]])
      row(solution,label,s[key]);
  } else row(solution,"未生成完整解法",s.unavailable_reason);
  $("counterexampleDetails").open = false; $("solutionDetails").open = false;
  const sourceLines = submitted.cpp_solution.split("\n");
  if (sourceLines.at(-1) === "") sourceLines.pop();
  $("sourceSnapshot").textContent = sourceLines.map((line,index) => (index + 1) + " | " + line).join("\n");
  $("sourceChangedNote").hidden = true;
  const metadata = $("metadata"); metadata.replaceChildren();
  for (const key of ["request_schema_version","response_schema_version","prompt_template_id","prompt_template_sha256",
    "source_code_sha256","code_location_basis","model_name","model_version","duration_ms","prompt_sha256","response_sha256"]) {
    metadata.append(textNode("dt","",key),textNode("dd","",body.metadata?.[key] ?? "null"));
  }
  metadata.append(textNode("dt","","request_id"),textNode("dd","",body.request_id));
  $("jsonText").value = JSON.stringify(body,null,2);
  $("jsonDetails").open = false; $("copyButton").textContent = "复制 JSON";
  lastJson = body; $("copyButton").hidden = false; showView("resultContent");
}
form.addEventListener("submit",async (event) => {
  event.preventDefault(); if (calling) return;
  $("formError").hidden = true;
  let payload;
  try { payload = buildPayload(); } catch (error) {
    $("formError").textContent = error.message; $("formError").hidden = false; return;
  }
  submitted = payload; lastJson = null; $("copyButton").hidden = true;
  setBusy(true); showView("loadingState");
  const controller = new AbortController(), timer = setTimeout(() => controller.abort(),120000);
  try {
    const response = await fetch("/api/diagnose",{method:"POST",headers:{"Content-Type":"application/json",Accept:"application/json"},
      body:JSON.stringify(payload),signal:controller.signal});
    let body;
    try { body = await response.json(); } catch (_) { showError("invalid_model_response","non_json"); return; }
    if (!response.ok || !body.ok) {
      showError(body.error?.kind || "server_error",body.error?.code || String(response.status),body.error?.message); return;
    }
    renderResult(body);
  } catch (error) {
    showError(error.name === "AbortError" ? "timeout" : "transport_error","");
  } finally { clearTimeout(timer); setBusy(false); }
});
$("clearButton").addEventListener("click",() => {
  if (calling) return;
  form.reset(); $("advancedOptions").open = false;
  lastJson = null; submitted = null; $("jsonText").value = ""; $("sourceSnapshot").textContent = "";
  $("copyButton").hidden = true; $("formError").hidden = true; showView("emptyState"); $("statement").focus();
});
$("exampleButton").addEventListener("click",() => {
  if (calling) return;
  form.reset(); $("advancedOptions").open = false;
  $("statement").value = "拿走尽可能少的硬币，使拿走的总价值严格大于剩余价值。\n输入：n 和 n 个面值。输出最少硬币数。\n约束：1 <= n <= 100，1 <= a[i] <= 100。";
  $("cppSolution").value = "#include <algorithm>\n#include <iostream>\n#include <vector>\nusing namespace std;\n\nint main() {\n    int n; cin >> n;\n    vector<int> coins(n);\n    int total = 0;\n    for (int &x : coins) { cin >> x; total += x; }\n    sort(coins.rbegin(), coins.rend());\n    int taken = 0, count = 0;\n    for (int x : coins) {\n        taken += x;\n        ++count;\n        if (taken * 2 >= total) break;\n    }\n    cout << count << '\\n';\n}\n";
  $("formError").hidden = true;
  if (lastJson) $("sourceChangedNote").hidden = false;
});
form.addEventListener("input",() => {
  if (lastJson) $("sourceChangedNote").hidden = false;
});
$("copyButton").addEventListener("click",async () => {
  if (!lastJson) return;
  try {
    await navigator.clipboard.writeText(JSON.stringify(lastJson,null,2));
    $("copyButton").textContent = "已复制";
  } catch (_) {
    $("jsonDetails").open = true; $("jsonText").focus(); $("jsonText").select();
    $("copyButton").textContent = "请手动复制已选 JSON";
  }
});
showView("emptyState");
checkHealth();
