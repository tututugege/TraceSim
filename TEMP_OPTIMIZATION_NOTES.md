# TraceSim Current Optimization Notes

这个文档是当前重构后版本的临时优化清单，目标不是做完整设计文档，而是记录现在最值得继续推进的几个方向，避免后续修改重新发散。

## 1. 当前状态概览

这次重构已经完成了第一阶段目标：

- `TraceSim` 不再是一个超大类，`Frontend`、`LoadStoreUnit`、`Profiler` 已经拆出。
- `TraceSim.cpp` 的主循环结构已经比较清晰，阶段上也分成了 `commit / issue / dispatch / decode / fetch`。
- 访存建模仍然保留了基本 cache state、in-flight request、fill return 机制。

当前主要问题不再是“代码全堆在一个文件里”，而是：

- 模块边界还不够干净。
- 预取功能已经恢复到“最小可用”，但实验入口和统计派生指标还不够完整。
- I-cache / D-cache 的层次访问逻辑仍有重复。
- top-down / profiling 目前偏粗。

## 2. 最高优先级问题

### P0. 预取和 load profiling 的最小功能已经恢复

这个问题现在已经完成，不再是当前最高优先级。

已完成内容：

- `record_load_event(...)` 已恢复最基本的 load 统计记账
- `enqueue_prefetches(...)` 已恢复
- `service_prefetch_queues()` 已恢复
- I-cache / D-cache prefetch queue 已重新接回
- prefetcher 配置项已回到 `SimConfig.h`
- prefetch 请求已经重新走 cache / LLC / fill 路径
- `print_stats()` 已能输出基础 prefetch 统计

当前已经具备的基础能力：

- `issued`
- `filtered_inflight`
- `dropped`
- `fills`
- `useful`
- `useless`
- `merged_with_prefetch`
- `late`
- queue 的 `filtered_hit / queue_dup / queue_drop`
- load profile 的 `total / dependent / stlf / l1_hit / l2_hit / dram / avg_latency`

### 当前限制

虽然 P0 已恢复，但目前仍然有这些限制：

- 默认配置仍然是 `NONE`，没有运行时命令行开关
- `load profile` 还是最小版，只做基础记账，没有按 PC 或 bucket 展开
- prefetch 统计还没有整理成 accuracy / coverage / traffic increase 这些实验常用指标
- I-cache / D-cache / LLC 的访问路径仍然是分开写的

### 下一步建议

1. 先增加运行时 prefetcher 切换入口，而不是每次改 `SimConfig.h`
2. 再补 accuracy / coverage / traffic increase 这类实验指标
3. 最后再考虑更细的 load profile 展开

## 3. 当前最高优先级问题

### P1. `Frontend` 和 `LoadStoreUnit` 仍然直接操作 `TraceSim` 内部状态

当前虽然已经拆文件，但仍然依赖：

- `friend class Frontend`
- `friend class LoadStoreUnit`

两个子模块直接读写：

- ROB
- IQ
- `inst_buffer`
- `pre_fetch_buffer`
- `fetch_stall_until`
- `reg_ready_time`
- `store_indices`
- cache 对象

这说明现在更像是“把一大段逻辑移到了别的 `.cpp`”，还不是边界清晰的子系统。

### 风险

- 状态协议是隐式的，不容易验证。
- 以后继续改前端或 LSU 时，还是容易把 `TraceSim` 耦合回去。
- 很难给 `Frontend` / `LoadStoreUnit` 单独做测试。

### 建议最小改法

先不要追求彻底 OO 化，只做接口收口：

- 给 `Frontend` 提供少量 helper：
  - `peek_trace_inst()`
  - `consume_trace_inst()`
  - `push_inst_buffer()`
  - `stall_fetch(...)`
- 给 `LoadStoreUnit` 提供少量 helper：
  - `request_dcache_line(...)`
  - `request_llc_line(...)`
  - `schedule_dcache_fill(...)`
  - `mark_load_complete(...)`

目标是逐步减少对 `TraceSim` 成员的直接访问，而不是一次性重写。

## 4. Cache hierarchy 访问逻辑重复

### P1. I-cache miss 路径和 D-cache miss 路径仍然各写一套

当前：

- `Frontend::fetch_stage()` 内部有一套 I-cache -> LLC -> fill 的逻辑。
- `LoadStoreUnit::execute_load_memory_access()` 内部有一套 D-cache -> LLC -> fill 的逻辑。

两边都在处理：

- `HIT`
- `MISS_NEW`
- `MISS_MERGED`
- `MISS_BLOCKED`
- LLC 命中/未命中
- `schedule_fill`
- `ready_cycle`

### 风险

- 两套逻辑后续容易漂移。
- 以后如果加端口竞争、prefetch merge、traffic 统计，要改两处。
- 容易出现 I-cache 和 D-cache 行为不一致，但代码表面看不出来。

### 建议最小改法

抽一个统一 helper，例如：

- `access_l1_hierarchy(Cache& l1, uint32_t addr, uint32_t l1_hit_latency, bool is_prefetch)`

返回统一结果结构，至少包含：

- `l1_status`
- `llc_status`
- `ready_cycle`
- `l1_hit`
- `llc_hit`
- `dram_miss`

然后让 Frontend 和 LSU 共用。

## 5. Profiler 还比较粗

### P1. 目前 profiler 更像“粗粒度打印器”

当前 `Profiler::mark_cycle()` 只大致区分：

- Retiring
- Frontend Bound
- Backend Memory Bound
- Backend Core Bound
- Bad Speculation
- Other

但实际上系统已经有更细的原因：

- 前端有 `ICACHE_MISS / FETCH_REDIRECT / BRANCH_MISPREDICT / LINE_BOUNDARY`
- 后端有 `ROB_FULL / ALU_IQ_FULL / LDU_IQ_FULL / STA_IQ_FULL / STD_IQ_FULL / BRU_IQ_FULL / TRAP_SERIALIZE / MEMORY_WAIT / EXEC_WAIT`

### 风险

- 现在输出适合快速看大方向，不适合分析结构性瓶颈。
- 很多已经建模出来的原因，没有体现在报告里。

### 建议最小改法

分两步：

1. 先在 `Profiler` 里保留二级细分计数，不急着改输出样式。
2. 稳定后再把输出整理成 tree 风格或层级风格。

优先级上，应该先让“统计正确”，再让“打印好看”。

## 6. `dispatch_stage()` 仍然混合“检查”和“分配”

### P1. 这个函数可以继续拆

当前 `dispatch_stage()` 里同时负责：

- 判断 ROB / IQ 是否满
- 判断 trap serialize
- 真正把 op 放进 ROB
- 真正把 op 放进对应 IQ
- 更新 rename 风格 writer tracking

### 风险

- 后续如果继续加 rename、dispatch latency、前后端更细的解耦，这里还会继续膨胀。

### 建议最小改法

拆成：

- `check_dispatch_hazard(const OpEntry&)`
- `dispatch_one_op(const OpEntry&)`

这样 `dispatch_stage()` 只保留循环和流控。

## 7. Cache 实现的性能优化是后续项

### P2. `Cache` 每次访问都构造 replacement policy 的 line view

这部分逻辑上是干净的，但软件实现会有额外开销。

尤其是：

- 每次 `request()` 都会构造 `views`
- 每次 `fill_block()` 也会构造 `views`

### 这不是当前首要问题

原因是：

- 这不直接影响功能正确性；
- 当前更大的问题是功能完整性和模块边界；
- 等前面几个 P0/P1 稳定后，再决定是否为了速度重构 replacement policy 接口。

## 8. 新增的直接优化项

### P1. 缺少运行时 prefetcher 切换入口

当前 prefetcher 类型已经重新回到 `SimConfig.h`，但还是编译期常量：

- `ICACHE_PREFETCHER`
- `DCACHE_PREFETCHER`
- `LLC_PREFETCHER`

### 风险

- 每次切换策略都要改源码再编译
- 不利于做批量实验
- 不利于和文档中描述的“可插拔接口”形成一致使用体验

### 建议最小改法

在 `main.cpp` 增加命令行选项，例如：

- `--icache-prefetcher none|next-line|pc-stride`
- `--dcache-prefetcher none|next-line|pc-stride`
- `--llc-prefetcher none|next-line|pc-stride`

这一步的收益很高，而且不会触碰核心流水线逻辑。

### P1. 缺少实验常用派生指标

现在能看到原始计数，但还缺少更直接的实验指标：

- prefetch accuracy
- prefetch coverage
- memory traffic increase

### 建议最小改法

基于当前已有计数直接派生：

- `accuracy = useful / fills` 或 `useful / issued`
- `coverage = useful / demand_miss`
- `traffic increase = prefetch_fill / demand_miss_new` 或按 LLC request 计算

先把定义写清楚，再输出。

## 9. 推荐执行顺序

### 已完成

- 恢复 `record_load_event()`、`enqueue_prefetches()`、`service_prefetch_queues()`
- 让预取和最小 load profiling 重新变成“真的在工作”的功能
- 恢复 prefetch queue、配置项和基础输出

### P1

- 增加运行时 prefetcher 切换入口
- 收紧 `Frontend` / `LoadStoreUnit` 对 `TraceSim` 的直接访问
- 统一 I-cache / D-cache / LLC 访问路径
- 细化 `Profiler` 的归因统计
- 增加 prefetch accuracy / coverage / traffic increase
- 拆分 `dispatch_stage()`

### P2

- 优化 `Cache` 内部实现开销
- 根据需要再考虑 replacement policy / prefetch queue / store search 的性能优化
- 再决定是否恢复更复杂的 load profile 输出

## 10. 一个现实建议

下一步不建议再做一次“大重构”。

更稳妥的做法是：

1. 先把实验入口补好，让 prefetch 可切换、可批量跑；
2. 再补实验更关心的派生指标；
3. 然后统一 cache hierarchy helper；
4. 最后再逐步收紧模块边界。

这样可以保证每一步都能运行、都能验证，不会再次进入“结构更漂亮但功能临时失效”的状态。
