# 新增 12 条候选：项目作者审核包

状态：**待项目作者逐项确认**。这些是原创改写题面/代码/测试，算法概念来源登记在 [`evaluation/expansion-20260828/sources.json`](../../evaluation/expansion-20260828/sources.json)。没有复制或执行网上代码。Planner 技术复核不等于真人审核。

## 题目摘要与约束

| 题目 | 摘要、关键约束与独立参考依据 |
| --- | --- |
| p09 最少并行工位（medium/dev） | n≤100000，半开区间且全部安排；按开始时间扫描，用最小堆释放 `end<=start` 的工位，答案为同时活跃数峰值。 |
| p10 最小最大延迟（medium/holdout） | n≤100000，所有任务必须做，延迟 `max(0,C-d)`；按截止时间升序（EDD）的相邻交换不增大最大延迟。 |
| p11 区间检查点（basic/dev） | n≤100000，闭整数区间；按右端点升序，遇到未覆盖区间就在其右端点放点，交换后不损害后续覆盖。 |
| p12 离线缓存装载（hard/holdout） | n≤100000，初始空缓存、无预取；缺页且满时淘汰下一次使用最晚（或不再使用）的页，交换不会增加后续缺页。 |

## 逐候选审核

| ID | 关键代码/说明 | 建议 gold（状态；类别；首次错） | 独立依据与固定测试 | 争议点 / 可选决定 |
| --- | --- | --- | --- | --- |
| s026 | 开始时间扫描、最小结束堆、记录峰值 | correct；—；— | 与 p09 扫描不变量一致；4/4 passed。 | 确认 / 修正 |
| s027 | L11 仅释放 `end < start` | incorrect；boundary_omission；L11 | 半开区间端点相接可复用；t1 应1、实际2；3/4 passed。 | 确认 / 修正 |
| s028 | L14 输出扫描结束时堆大小 | incorrect；code_logic_error；L14 | 应记录全过程峰值；t2 应3、实际1；2/4 passed。 | 确认 / 修正 |
| s029 | 按截止时间升序执行 | correct；—；— | 与 EDD 相邻交换依据一致；4/4 passed。 | 确认 / 修正 |
| s030 | L8 按任务时长排序 | incorrect；wrong_greedy_choice；L8 | `6,6` 与 `1,100` 应先前者、最大延迟0；实际排序给1；3/4 passed。 | 确认 / 修正 |
| s031 | L11 累加每项延迟 | incorrect；code_logic_error；L11 | 题目要求最大值而非总和；t2 应3、实际4；2/4 passed。 | 主类是否应为 problem_misunderstanding |
| s032 | 右端点排序，未覆盖时选右端点 | correct；—；— | 与 p11 交换依据一致；4/4 passed。 | 确认 / 修正 |
| s033 | L10 用 `left>=point` 判未覆盖 | incorrect；boundary_omission；L10 | 闭区间包含当前端点；t1 应1、实际2；2/4 passed。 | 确认 / 修正 |
| s034 | L12 选择当前左端点 | incorrect；wrong_greedy_choice；L12 | 左端点给后续区间的覆盖最弱；t1 应1、实际2；2/4 passed。 | 确认 / 修正 |
| s035 | 淘汰下一次使用最晚页面 | correct；—；— | 与 Belady 交换依据一致；4/4 passed。 | 确认 / 修正 |
| s036 | L12 满时任意删 `cache.begin()` | incorrect；wrong_greedy_choice；L12 | 序列 `1 2 3 1 2 3`, k=2 应4、实际6；3/4 passed。 | 确认 / 修正 |
| s037 | 代码同正确版；说明称任意淘汰都最优 | incorrect；invalid_greedy_proof；文字首错 | 上述序列中 Belady 为4次而任意策略可6次；代码4/4 passed。 | 是否把错误说明独立计为过程错 |

## 作者回复模板

可直接回复：`新增12条全部按建议确认，审核日期 YYYY-MM-DD。` 若只确认正式候选，请明确写：`确认 s037（以及正式包列出的其它 ID）`；未确认的新材料仍作为附录草案。
