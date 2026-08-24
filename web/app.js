"use strict";

const CATEGORY_LABELS = {
  problem_misunderstanding: "题意理解错误",
  wrong_greedy_choice: "贪心选择错误",
  missing_greedy_proof: "缺少贪心正确性证明",
  invalid_greedy_proof: "贪心证明无效",
  complexity_error: "复杂度错误",
  boundary_omission: "边界遗漏",
  implementation_mismatch: "思路与实现不一致"
};
const STATUS_LABELS = { correct: "正确", incorrect: "错误", undetermined: "无法确定" };
const STAGES = [
  ["problem_understanding", "题意理解"], ["greedy_choice", "贪心选择"],
  ["greedy_proof", "贪心证明"], ["complexity", "复杂度"],
  ["boundary", "边界条件"], ["implementation_consistency", "思路与代码一致性"]
];

const $ = (id) => document.getElementById(id);
const form = $("diagnosisForm");
let lastJson = null;
let calling = false;

function setHealth(kind, text) {
  const el = $("healthStatus");
  el.className = `health ${kind}`;
  el.lastChild.textContent = text;
}

async function checkHealth() {
  try {
    const response = await fetch("/api/health", { method: "GET", headers: { Accept: "application/json" }, cache: "no-store" });
    if (!response.ok) throw new Error("health unavailable");
    const body = await response.json();
    const available = body.ok === true && body.tokenhub_status === "configured";
    setHealth(available ? "ok" : "error", available ? "TokenHub 已配置" : "TokenHub 未配置");
  } catch (_) {
    setHealth("error", "本地服务未连接");
  }
}

function newRequestId() {
  if (globalThis.crypto?.randomUUID) return `interactive-${crypto.randomUUID()}`;
  return `interactive-${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

function value(id) { return $(id).value.trim(); }
function optionalRaw(id) {
  const raw = $(id).value;
  return raw.trim() ? raw : null;
}

function buildPayload() {
  const testInput = optionalRaw("testInput");
  const expectedOutput = optionalRaw("expectedOutput");
  return {
    request_id: newRequestId(),
    algorithm_type: "greedy",
    problem: {
      title: value("title"), statement: value("statement"),
      input_format: value("inputFormat"), output_format: value("outputFormat"),
      constraints: value("constraints")
    },
    reasoning: value("reasoning"),
    cpp_solution: optionalRaw("cppSolution"),
    test_cases: (testInput || expectedOutput) ? [{ input: testInput || "", expected_output: expectedOutput || "" }] : [],
    user_notes: optionalRaw("userNotes")
  };
}

function setBusy(busy) {
  calling = busy;
  $("submitButton").disabled = busy;
  $("exampleButton").disabled = busy;
  $("clearButton").disabled = busy;
  $("submitButton").classList.toggle("loading", busy);
  $("submitButton").querySelector(".button-label").textContent = busy ? "诊断中…" : "开始诊断";
}

function showView(view) {
  for (const id of ["emptyState", "loadingState", "errorState", "resultContent"]) $(id).hidden = id !== view;
}

function friendlyError(kind, code, fallback) {
  const key = String(kind || code || "").toLowerCase();
  if (key.includes("auth") || key.includes("unauthorized") || key === "401" || key === "403") return ["TokenHub 认证失败", "服务端凭证不可用。请检查本地服务端环境配置；浏览器不会接触该凭证。"];
  if (key.includes("rate") || key === "429") return ["调用频率受限", "TokenHub 返回 rate limit。本次不会自动重试，请稍后手动再试。"];
  if (key.includes("timeout")) return ["模型调用超时", "本次调用已超时并结束，页面不会自动重试。"];
  if (key.includes("transport") || key.includes("network")) return ["网络传输失败", "本地服务与 TokenHub 通信失败，请检查网络后手动再试。"];
  if (key.includes("parse") || key.includes("schema") || key.includes("semantic") || key.includes("diagnos") || key.includes("invalid_model")) return ["模型诊断响应无效", fallback || "模型返回内容未通过严格 JSON / schema / semantic 校验，未进行自动修复。"];
  return ["请求失败", fallback || "本地服务未能完成诊断。"];
}

function showError(kind, code, message) {
  const [title, detail] = friendlyError(kind, code, message);
  $("errorTitle").textContent = title;
  $("errorMessage").textContent = detail;
  $("errorCode").textContent = code || kind || "unknown_error";
  $("copyButton").hidden = true;
  showView("errorState");
}

function appendText(tag, className, text) {
  const el = document.createElement(tag);
  if (className) el.className = className;
  el.textContent = text ?? "—";
  return el;
}

function assessmentText(value, fallback) {
  if (typeof value === "string") return value;
  if (value && typeof value === "object") return value.summary || value.assessment || value.message || JSON.stringify(value);
  return fallback;
}

function renderFindings(findings) {
  const root = $("findingsGroups"); root.replaceChildren();
  const list = Array.isArray(findings) ? findings : [];
  $("findingCount").textContent = `${list.length} 项`;
  for (const [stageKey, stageLabel] of STAGES) {
    const group = appendText("div", "stage-group", "");
    const heading = appendText("div", "stage-heading", "");
    heading.append(appendText("strong", "", stageLabel), appendText("small", "", stageKey));
    group.append(heading);
    const matches = list.filter((item) => item && item.stage === stageKey);
    if (!matches.length) group.append(appendText("div", "stage-empty", "未报告该阶段问题"));
    for (const item of matches) {
      const card = appendText("article", "finding-card", "");
      card.append(appendText("span", "finding-category", CATEGORY_LABELS[item.category] || item.category || "未分类"));
      const dl = document.createElement("dl");
      const rows = [
        ["定位", item.locating || item.location], ["证据", item.evidence],
        ["输入片段", item.input_excerpt || item.source_excerpt || item.excerpt], ["建议", item.suggestion]
      ];
      for (const [label, text] of rows) if (text) dl.append(appendText("dt", "", label), appendText("dd", "", text));
      card.append(dl); group.append(card);
    }
    root.append(group);
  }
}

function renderAssessments(diagnosis) {
  const root = $("assessments"); root.replaceChildren();
  const a = diagnosis.assessments || {};
  const findings = Array.isArray(diagnosis.findings) ? diagnosis.findings : [];
  const specs = [
    ["复杂度", "complexity", "complexity_error"],
    ["边界条件", "boundary_conditions", "boundary_omission"],
    ["思路与代码一致性", "implementation_consistency", "implementation_mismatch"]
  ];
  for (const [label, key, category] of specs) {
    const relevant = findings.filter((f) => f?.category === category);
    let fallback = relevant.length ? `发现 ${relevant.length} 项相关问题` : "未报告相关问题";
    if (key === "implementation_consistency" && !value("cppSolution")) fallback = "未提供代码，本阶段不适用";
    const card = appendText("div", "assessment", "");
    card.append(appendText("span", "", label), appendText("p", "", assessmentText(a[key], fallback)));
    root.append(card);
  }
}

function addMetadata(label, value) {
  if (value === undefined || value === null || value === "") return;
  $("metadata").append(appendText("dt", "", label), appendText("dd", "", String(value)));
}

function renderResult(body) {
  const diagnosis = body.diagnosis || body.prediction || body.result?.diagnosis;
  if (!diagnosis || !STATUS_LABELS[diagnosis.status] || !Array.isArray(diagnosis.findings)) {
    showError("diagnosis_error", body.parse_status, "服务端响应缺少有效的结构化 diagnosis。"); return;
  }
  const metadata = body.metadata || {};
  const status = diagnosis.status;
  $("statusCard").className = `status-card ${status}`;
  $("statusText").textContent = STATUS_LABELS[status]; $("statusCode").textContent = status;
  const category = diagnosis.primary_category;
  $("primaryCategory").textContent = category ? (CATEGORY_LABELS[category] || category) : "无";
  $("primaryCategoryCode").textContent = category || "null";
  renderFindings(diagnosis.findings); renderAssessments(diagnosis);
  $("shortSuggestion").textContent = diagnosis.short_suggestion || diagnosis.summary || (status === "correct" ? "未发现需要修正的错误。" : "请根据各阶段 finding 逐项修正并重新论证。 ");
  $("metadata").replaceChildren();
  addMetadata("模型", metadata.model_name || "hy3");
  addMetadata("模型版本", metadata.model_version ?? "null");
  addMetadata("耗时", metadata.duration_ms == null ? null : `${metadata.duration_ms} ms`);
  addMetadata("request_id", diagnosis.request_id || body.request_id);
  addMetadata("Provider request_id", metadata.provider_request_id);
  addMetadata("Prompt SHA-256", metadata.prompt_sha256);
  addMetadata("Response SHA-256", metadata.response_sha256 || metadata.raw_response_sha256);
  if (metadata.token_usage) addMetadata("Token", `${metadata.token_usage.prompt_tokens ?? "?"} + ${metadata.token_usage.completion_tokens ?? "?"} = ${metadata.token_usage.total_tokens ?? "?"}`);
  addMetadata("parse_status", body.parse_status);
  lastJson = body; $("copyButton").hidden = false; showView("resultContent");
}

form.addEventListener("submit", async (event) => {
  event.preventDefault(); if (calling) return;
  $("formError").hidden = true;
  if (!form.reportValidity()) return;
  const payload = buildPayload();
  setBusy(true); $("copyButton").hidden = true; showView("loadingState");
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 120000);
  try {
    const response = await fetch("/api/diagnose", { method: "POST", headers: { "Content-Type": "application/json", Accept: "application/json" }, body: JSON.stringify(payload), signal: controller.signal });
    let body;
    try { body = await response.json(); } catch (_) { throw Object.assign(new Error("服务端返回的不是 JSON。"), { kind: "diagnosis_error" }); }
    if (!response.ok || body.ok === false || body.outcome === "error") {
      const error = body.error || {};
      showError(error.kind || `http_${response.status}`, error.code || String(response.status), error.message); return;
    }
    renderResult(body);
  } catch (error) {
    showError(error.name === "AbortError" ? "timeout" : (error.kind || "transport_error"), "", error.message);
  } finally { clearTimeout(timer); setBusy(false); }
});

$("clearButton").addEventListener("click", () => {
  if (calling) return; form.reset(); document.querySelectorAll("[data-count-for]").forEach((o) => o.textContent = "0");
  lastJson = null; $("copyButton").hidden = true; $("formError").hidden = true; showView("emptyState"); $("title").focus();
});

$("exampleButton").addEventListener("click", () => {
  if (calling) return;
  const example = {
    title: "CF 160A — Twins",
    statement: "两个双胞胎要分一堆硬币。每枚硬币有一个正整数面值。你需要拿走尽可能少的硬币，同时让你拿走的硬币总价值严格大于剩余硬币的总价值。求最少硬币数。",
    inputFormat: "第一行一个整数 n，表示硬币数量。\n第二行 n 个整数 a₁…aₙ，表示每枚硬币的面值。",
    outputFormat: "输出一个整数：为使拿走的价值严格大于剩余价值，至少需要拿走的硬币数。",
    constraints: "1 ≤ n ≤ 100；1 ≤ aᵢ ≤ 100。",
    reasoning: "将所有硬币按面值从大到小排序，依次拿走最大的硬币。累计价值必须严格大于全部硬币总价值的一半时才停止。这样每一步都用一枚硬币获得当前最大的增量，因此硬币数量最少。排序复杂度为 O(n log n)。",
    cppSolution: "#include <bits/stdc++.h>\nusing namespace std;\n\nint main() {\n    ios::sync_with_stdio(false);\n    cin.tie(nullptr);\n    int n;\n    cin >> n;\n    vector<int> coins(n);\n    int total = 0;\n    for (int &x : coins) { cin >> x; total += x; }\n    sort(coins.rbegin(), coins.rend());\n    int taken = 0, count = 0;\n    for (int x : coins) {\n        taken += x;\n        ++count;\n        if (taken * 2 >= total) break;\n    }\n    cout << count << '\\n';\n}\n",
    testInput: "2\n5 5\n", expectedOutput: "2\n",
    userNotes: "请重点检查总价值恰好达到一半时的边界处理。"
  };
  for (const [id, text] of Object.entries(example)) $(id).value = text;
  document.querySelectorAll("[data-count-for]").forEach((o) => o.textContent = $(o.dataset.countFor).value.length);
  $("cppSolution").closest("details").open = true; $("testInput").closest("details").open = true;
});

$("copyButton").addEventListener("click", async () => {
  if (!lastJson) return;
  try { await navigator.clipboard.writeText(JSON.stringify(lastJson, null, 2)); $("copyButton").textContent = "已复制"; setTimeout(() => $("copyButton").textContent = "复制 JSON", 1400); }
  catch (_) { $("copyButton").textContent = "复制失败"; }
});

for (const output of document.querySelectorAll("[data-count-for]")) {
  const input = $(output.dataset.countFor); input.addEventListener("input", () => { output.textContent = input.value.length; });
}

showView("emptyState");
checkHealth();
