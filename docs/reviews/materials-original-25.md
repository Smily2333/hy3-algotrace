# 原始 25 条候选：项目作者审核包

状态：**待项目作者逐项确认**。Planner 只做技术复核，不在此处代签真人审核。固定测试是已保存的隔离执行证据；通过有限测试不等于完整正确。建议回复方式见文末。

## 题目摘要与约束

| 题目 | 摘要、关键约束与独立参考依据 |
| --- | --- |
| p01 数额选择（basic/dev） | n≤100000，正整数；选最少项使所选和严格大于其余。降序取前缀：任意 k 项和不超过最大 k 项和。 |
| p02 等待安排（basic/holdout） | n≤100000，正时长；最小化所有任务开始前等待和。相邻交换说明短任务应在长任务之前。 |
| p03 单厅预约（medium/dev） | n≤100000，半开区间 `[s,e)`，相接允许；按结束时间最早选择，交换不会减少后续空间。 |
| p04 合并账单（medium/holdout） | n≤100000，正重量；每次合并两堆并累加代价。最小两堆先合并的交换论证即 Huffman 合并。 |
| p05 递增补齐（medium/holdout） | n≤100000，只能增加并保持顺序；逐项取 `max(a[i], prev+1)` 是当前最小可行值，也不给后续增加额外自由。 |
| p06 期限内任务（hard/holdout） | n≤100000，可选任务，完成时刻≤期限；按期限扫描，超期时删除已选最长任务，保留同数量下最小总时长。 |
| p07 最低补给次数（hard/holdout） | n≤100000，容量无限；无法继续时从所有已到达站点选最大油量，交换只会扩大可达范围。 |
| p08 容量配对（hard/holdout） | n≤100000，每箱最多两件且和≤B；最重必须占一箱，若能配最轻则配对，否则单装。 |

## 逐候选审核

| ID | 关键代码/说明 | 建议 gold（状态；类别；首次错） | 独立依据与固定测试 | 争议点 / 可选决定 |
| --- | --- | --- | --- | --- |
| s001 | 降序排序后严格过半 | correct；—；— | 与 p01 交换依据一致；3/3 passed。作者此前已确认，但记录缺精确审核时间。 | 确认 / 修正；补审核时间 |
| s002 | L9 `if(2*s>=sum)` | incorrect；boundary_omission；L9 | `2 / 5 5` 应为2，实际1；2/3 passed。作者此前已确认。 | 确认 / 修正；补审核时间 |
| s003 | L8 升序排序 | incorrect；wrong_greedy_choice；L8 | `3 / 8 1 1` 应为1，实际3；2/3 passed。作者此前已确认。 | 确认 / 修正；补审核时间 |
| s004 | 时长升序并累加先前等待 | correct；—；— | 与 p02 相邻交换依据一致；3/3 passed。 | 确认 / 修正 |
| s005 | L8 时长降序 | incorrect；wrong_greedy_choice；L8 | `3 / 3 1 2` 应为4，实际8；2/3 passed。 | 确认 / 改类别/定位 |
| s006 | L9 先累加时长再计入答案 | incorrect；problem_misunderstanding；L9 | 计算完成时间和而非等待时间和；同例应4、实际10；0/3 passed。 | 主类是否应为实现逻辑 |
| s007 | 按结束时间排序，相接用 `>=` | correct；—；— | 与 p03 交换依据一致；3/3 passed。 | 确认 / 修正 |
| s008 | L9 `start > end` | incorrect；boundary_omission；L9 | 半开区间允许相接；t1 应2、实际1；1/3 passed。 | 确认 / 修正 |
| s009 | L8 按开始时间排序 | incorrect；wrong_greedy_choice；L8 | 长区间会挡住多个短区间；t2 应2、实际1；2/3 passed。 | 确认 / 修正 |
| s010 | 最小堆反复合并两最小值 | correct；—；— | 与 p04 Huffman 交换依据一致；3/3 passed。 | 确认 / 修正 |
| s011 | L7 使用最大堆 | incorrect；wrong_greedy_choice；L7 | `1 2 8` 应14，实际21；1/3 passed。 | 确认 / 修正 |
| s012 | L8 `ans=a+b` 覆盖累计值 | incorrect；code_logic_error；L8 | 少计先前合并费用；同例应14、实际11；1/3 passed。 | 确认 / 修正 |
| s013 | 每项提升到 `prev+1` | correct；—；— | 与 p05 最小可行前缀不变量一致；3/3 passed。 | 确认 / 修正 |
| s014 | L8 仅提升到 `prev` | incorrect；boundary_omission；L8 | 严格递增被错作非递减；`3 1 1` 应7、实际4；1/3 passed。 | 确认 / 修正 |
| s015 | L9 `prev=x` 保存原值 | incorrect；code_logic_error；L9 | 后续约束应基于已提升值；同例应7、实际4；2/3 passed。 | 确认 / 修正 |
| s016 | 按期限扫描，超期删最长 | correct；—；— | 与 p06 保留最小总时长不变量一致；3/3 passed。 | 确认 / 修正 |
| s017 | L9 `now>=deadline` 即删除 | incorrect；boundary_omission；L9 | 恰好按期完成应允许；t1 应2、实际1；1/3 passed。 | 确认 / 修正 |
| s018 | L9 超期时删最短 | incorrect；wrong_greedy_choice；L9 | 删除最长才能最大幅度释放时间；t1 应2、实际1；2/3 passed。 | 确认 / 修正 |
| s019 | 到不了时从可达站取最大油量 | correct；—；— | 与 p07 延迟决策及交换依据一致；4/4 passed。 | 确认 / 修正 |
| s020 | L8 只纳入 `position < reach` | incorrect；boundary_omission；L8 | 油量恰好到站时该站可用；t1 应2、实际-1；3/4 passed。 | 确认 / 修正 |
| s021 | L8 使用最小堆补给 | incorrect；wrong_greedy_choice；L8 | 取小油量可能增加次数；t4 应1、实际3；3/4 passed。 | 确认 / 修正 |
| s022 | 排序后最重尝试配最轻 | correct；—；— | 与 p08 最重物品交换依据一致；3/3 passed。 | 确认 / 修正 |
| s023 | L9 仅当 `light+heavy < B` 配对 | incorrect；boundary_omission；L9 | 容量允许等于 B；t1 应2、实际3；1/3 passed。 | 确认 / 修正 |
| s024 | L8 冒泡排序双重循环 | incorrect；complexity_error；L8 | 逻辑输出通过3/3，但 n=100000 时 O(n²) 不满足预期规模；未做压力运行。 | 是否认可复杂度为过程错误 |
| s025 | 代码正确；说明称“任意 k 项总和相同” | incorrect；invalid_greedy_proof；文字首错 | 例如价值 `8,1,1` 的一项和并不相同；代码仍3/3 passed。 | 是否把错误说明独立计为过程错 |

## 作者回复模板

可直接回复：`原25条全部按建议确认；s001–s003实际审核日期为 YYYY-MM-DD。` 若不同意，只需列出例外，例如：`s006 主类别改为 code_logic_error，理由……`。未明确确认的条目继续保持 pending。
