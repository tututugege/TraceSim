# TraceSim 当前预取实现

这份文档描述当前仓库里已经实现的预取框架，不讨论未来理想方案。

## 1. 当前预取路径

当前预取逻辑由 [trace_sim/mem/MemSubsystem.h](/home/tututu/TraceSim/trace_sim/mem/MemSubsystem.h) 和 [trace_sim/mem/MemSubsystem.cpp](/home/tututu/TraceSim/trace_sim/mem/MemSubsystem.cpp) 统一管理。

触发点有两个：

- `Frontend::fetch_stage()`
  - I-Cache 访问后调用 `mem->enqueue_prefetch(false, pc, addr, hit)`
- `LoadStoreUnit::execute_load_memory_access()`
  - D-Cache load 访问后调用 `mem->enqueue_prefetch(true, pc, addr, hit)`

也就是说，当前预取器是由 demand 访问触发训练和发起的。

## 2. 当前已经实现的最小能力

当前预取实验依赖的关键能力已经有了：

- 有状态 I$ / D$ / LLC
- demand miss 的 in-flight 跟踪
- demand merge
- prefetch request 正常走 request / fill 路径
- line 返回前不会被误判成 hit
- useful / useless / late prefetch 基础统计
- I$/D$ 各自独立的 prefetch queue

这已经足够做：

- `next-line`
- `PC-based stride`

这类基础预取器的趋势性实验。

## 3. 当前 prefetcher 接口

接口在 [trace_sim/mem/Prefetcher.h](/home/tututu/TraceSim/trace_sim/mem/Prefetcher.h)。

输入：

```cpp
struct PrefetcherAccessInfo {
    uint32_t pc = 0;
    uint32_t addr = 0;
    uint32_t line_size = 0;
    bool is_instruction = false;
    bool is_load = false;
    bool is_store = false;
    bool hit = false;
    bool miss = false;
};
```

输出：

```cpp
struct PrefetchRequest {
    uint32_t addr = 0;
};
```

接口函数：

```cpp
virtual void on_access(const PrefetcherAccessInfo &info,
                       std::vector<PrefetchRequest> &requests) = 0;
```

设计意图很简单：

- prefetcher 只根据一次 demand 访问生成候选地址
- 它不直接改 cache
- 它也不直接决定请求一定会发出去

真正的过滤、排队和发出由 `MemSubsystem` 完成。

## 4. 当前 request / queue / fill 流程

当前流程可以概括成：

1. demand 访问 I$ 或 D$
2. `MemSubsystem` 调用对应 L1 cache
3. prefetcher 根据访问上下文生成候选地址
4. `enqueue_prefetch()` 先做一轮基础过滤
5. 合法请求进入 I$/D$ 的 prefetch queue
6. 周期末 `service_prefetch_queues()` 尝试真正发出 prefetch
7. prefetch 进入 L1 / LLC 的正常 request 路径
8. 到 `ready_cycle` 后，由 `process_returns()` 真正 fill 到 cache

当前过滤主要包括：

- 已经在 cache 中
- 已经在 prefetch queue 中重复出现
- queue 满

更深一层的去重则由 `Cache::request()` 的 in-flight merge 负责。

## 5. 当前 cache 如何支持预取统计

当前 [trace_sim/mem/Cache.h](/home/tututu/TraceSim/trace_sim/mem/Cache.h) 里有两类关键状态。

### 5.1 Line metadata

每条 line 里有：

- `prefetched`
- `used_by_demand`
- `fill_cycle`

它们支持：

- 识别这条 line 是否由 prefetch 带回
- 判断后续是否真的被 demand 用到
- 在 eviction 时判断是否属于 useless prefetch

### 5.2 In-flight request

当前 `InflightRequest` 至少记录：

- `block_addr`
- `ready_cycle`
- `is_prefetch`
- `has_demand_waiter`

它们支持：

- demand miss merge
- demand merge 到 prefetch request
- late prefetch 识别

## 6. 当前已支持的统计

当前 `MemSubsystem::print_stats()` 会输出原始 cache 统计，至少包括：

- `hit/access`
- `miss_new`
- `miss_merged`
- `miss_blocked`
- `prefetch issued`
- `prefetch hit`
- `prefetch dropped`
- `prefetch fills`
- `filtered_inflight`
- `useful`
- `useless`
- `merged_with_prefetch`
- `late`

以及 prefetch queue 侧统计：

- `filtered_hit`
- `queue_dup`
- `queue_drop`

这套统计还不是完整的论文级实验指标，但已经足够支持基础 debug 和趋势判断。

## 7. 当前已内置的预取器

当前内置：

- `NullPrefetcher`
- `NextLinePrefetcher`
- `PcStridePrefetcher`
- `TemplatePrefetcher`

其中 `TemplatePrefetcher` 只是给你抄骨架用，不会默认启用。

## 8. 当前没有实现的东西

如果你要把实验进一步做严谨，后面还可以补：

- 独立 prefetch MSHR 配额
- demand / prefetch 更明确的优先级
- memory traffic 派生统计
- accuracy / coverage / timeliness 等派生指标
- LLC prefetcher 的真正训练和协同策略

## 9. 如何加自己的 prefetcher

最简单的方式是：

1. 在 [trace_sim/mem/Prefetcher.h](/home/tututu/TraceSim/trace_sim/mem/Prefetcher.h) 里复制 `TemplatePrefetcher`
2. 实现自己的 `on_access()`
3. 在 `PrefetcherType` 里加一个新枚举
4. 在 `make_prefetcher()` 里加一个 `case`
5. 在 [trace_sim/SimConfig.h](/home/tututu/TraceSim/trace_sim/SimConfig.h) 里切到你的实现

如果只是做基础实验，建议先只改 D-Cache prefetcher，不要一开始同时改 I$、D$、LLC 三层。
