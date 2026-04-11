# TraceSim 预取实验最小实现指南

这份文档的目标不是把 TraceSim 做成 ChampSim，而是把它补到一个“足够支持基础硬件预取实验”的程度。

这里的“基础预取实验”指的是：

- 能接入至少 `next-line prefetcher` 和 `PC-based stride prefetcher`
- 能比较“开/关预取器”对 IPC、miss、traffic 的影响
- 能统计 `useful/useless/late`，而不是只看 hit rate
- 不把预取建模成“零成本、立即生效”的理想化机制

## 1. 当前状态

当前仓库已经具备的基础：

- 有状态 cache：[`trace_sim/Cache.h`](/home/tututu/TraceSim/trace_sim/Cache.h)
- 可插拔 replacement policy：[`trace_sim/ReplacementPolicy.h`](/home/tututu/TraceSim/trace_sim/ReplacementPolicy.h)
- 可插拔 prefetcher 接口：[`trace_sim/Prefetcher.h`](/home/tututu/TraceSim/trace_sim/Prefetcher.h)
- CPU 流水线是 cycle-based：[`trace_sim/TraceSim.cpp`](/home/tututu/TraceSim/trace_sim/TraceSim.cpp)

当前最关键的问题：

- miss 时 line 会立即安装进 cache，不存在“在途 miss”
- prefetch 会直接同步填入 cache，不占资源
- 没有 MSHR / in-flight miss table
- 没有 useful/useless/late 统计
- prefetcher 接口缺少 `PC`、访问类型等训练信息

所以当前模型适合“快速验证思路”，不适合“可信评估预取器收益”。

## 2. 最小必要功能清单

要支持基础预取实验，至少需要实现下面这些能力：

1. `Cache line metadata`
2. `MSHR / in-flight request tracking`
3. `请求发出 -> 未来返回 -> fill` 的动态过程
4. `prefetch` 和 `demand` 共用资源路径
5. `prefetch 去重与过滤`
6. `prefetcher` 训练输入扩展
7. `useful/useless/late/coverage/accuracy` 统计

推荐实现顺序：

- P0：`metadata + MSHR + return path`
- P1：`demand/prefetch arbitration + 去重 + 统计`
- P2：`PC-based stride prefetcher + 更完整实验输出`

---

## 3. 分步实现

### Step 1：给 cache line 加预取元数据

目标：

- 区分 demand fill 和 prefetch fill
- 判断某条 prefetched line 后续是否被 demand 真正用到
- 判断 prefetched line 是否在未被 demand 使用前就被替换掉

建议修改位置：

- [`trace_sim/Cache.h`](/home/tututu/TraceSim/trace_sim/Cache.h)

最小实现：

在 `Cache::Line` 中增加：

```cpp
bool prefetched = false;
bool used_by_demand = false;
uint64_t fill_cycle = 0;
```

行为规则：

- demand fill：
  - `prefetched = false`
  - `used_by_demand = true`
- prefetch fill：
  - `prefetched = true`
  - `used_by_demand = false`
- demand hit 一个 `prefetched && !used_by_demand` 的 line：
  - 记一次 `useful_prefetch`
  - 置 `used_by_demand = true`
  - 可选择保留 `prefetched=true` 仅作调试，或清零避免重复计数
- eviction 一个 `prefetched && !used_by_demand` 的 line：
  - 记一次 `useless_prefetch`

为什么必须做：

- 没这一步，就无法区分“预取真的帮上忙”和“只是恰好命中了 cache”

完成判据：

- 能统计：
  - `prefetch_fill`
  - `useful_prefetch`
  - `useless_prefetch`

### Step 2：把 miss 从“立即装入 cache”改成“进入在途请求”

目标：

- cache miss 后不立即变成 hit
- 为 demand-demand merge、demand-prefetch merge 建立基础

建议新增：

- 新头文件：`trace_sim/Mshr.h`
- 或直接先放进 [`trace_sim/Cache.h`](/home/tututu/TraceSim/trace_sim/Cache.h)

最小数据结构：

```cpp
struct InflightRequest {
    uint32_t block_addr = 0;
    uint64_t ready_cycle = 0;
    bool is_prefetch = false;
    bool has_demand_waiter = false;
};
```

cache 内新增：

```cpp
std::unordered_map<uint32_t, InflightRequest> inflight;
size_t max_mshrs = 16;
```

行为规则：

- demand miss：
  - 如果 block 已在 `inflight`，则 merge，不新发请求
  - 否则新建 `InflightRequest`
- prefetch：
  - 也进入同一套 `inflight`
- line 只有在 `ready_cycle <= current_cycle` 时才 fill 到 cache

为什么必须做：

- 预取实验里最容易出错的地方就是把“在途 line”提前当成 hit

完成判据：

- 同一个 block 的第二次访问，不会再错误地产生第二个独立 miss
- 同一个 block 在返回前不会被 `Cache::access()` 直接认成 hit

### Step 3：增加真实的返回路径

目标：

- 请求发出和 line fill 之间有真实时间差
- 返回前 line 不在 tag array 中

建议修改位置：

- [`trace_sim/Cache.h`](/home/tututu/TraceSim/trace_sim/Cache.h)
- [`trace_sim/TraceSim.cpp`](/home/tututu/TraceSim/trace_sim/TraceSim.cpp)

最小实现方案有两种：

方案 A：每个 cache 自己维护 completion queue

```cpp
std::deque<InflightRequest> completion_queue;
```

方案 B：在 `TraceSim` 顶层维护 memory return event queue

```cpp
struct MemoryReturnEvent {
    Cache *target_cache;
    uint32_t block_addr;
    uint64_t ready_cycle;
    bool is_prefetch;
};
```

推荐：

- 先做方案 A，改动更小

行为规则：

- `access()` 不再直接 `probe_and_fill()`
- miss 时只创建 in-flight request，并返回 miss 类型
- 每个周期推进时调用：

```cpp
icache.process_returns(total_cycles);
dcache.process_returns(total_cycles);
llc.process_returns(total_cycles);
```

- `process_returns()` 里负责真正安装 line

为什么必须做：

- 没有 return path，`late prefetch` 根本没法定义

完成判据：

- 能区分：
  - cache hit
  - merged miss
  - new miss
- 只有返回后 line 才出现在 cache 中

### Step 4：让 load/fetch 看到更细的访问结果，而不是只返回 bool

目标：

- 不能只知道 hit/miss
- 必须区分：
  - `HIT`
  - `MISS_NEW`
  - `MISS_MERGED`
  - `MISS_MERGED_WITH_PREFETCH`

建议修改位置：

- [`trace_sim/Cache.h`](/home/tututu/TraceSim/trace_sim/Cache.h)
- [`trace_sim/TraceSim.cpp`](/home/tututu/TraceSim/trace_sim/TraceSim.cpp)

建议定义：

```cpp
enum class CacheAccessResultType {
    HIT,
    MISS_NEW,
    MISS_MERGED,
    MISS_MERGED_WITH_PREFETCH,
    PREFETCH_REJECTED
};

struct CacheAccessResult {
    CacheAccessResultType type;
    bool l1_hit = false;
    bool llc_hit = false;
    uint64_t ready_cycle = 0;
};
```

然后把：

```cpp
bool l1_hit = dcache.access(op.inst.mem_addr, total_cycles);
```

改成：

```cpp
CacheAccessResult res = dcache.access(op.inst.mem_addr, total_cycles, AccessKind::DEMAND_LOAD, op.inst.pc);
```

为什么必须做：

- 只有这样，CPU 才能知道这次 demand 是“真的新 miss”还是“merge 到已有 prefetch 上”

完成判据：

- load latency 不再靠“现场查 hit/miss 然后手算固定 penalty”
- 而是由 memory system 返回 `ready_cycle`

### Step 5：prefetch 也必须走正常资源路径

目标：

- prefetch 不能零成本生效
- prefetch 要和 demand 竞争资源，但 demand 优先

建议新增结构：

```cpp
std::deque<PrefetchRequest> prefetch_queue;
size_t max_prefetch_queue = 16;
size_t max_outstanding_prefetch = 8;
```

行为规则：

- prefetcher 只“产生命中候选地址”
- cache/memory 子系统决定：
  - 是否发出
  - 是否丢弃
  - 是否节流
- 如果 MSHR 满：
  - demand 可以优先占用
  - prefetch 丢弃或延后

为什么必须做：

- 否则 aggressive prefetcher 不会付出任何代价，实验会系统性高估收益

完成判据：

- 能统计：
  - `prefetch_issued`
  - `prefetch_dropped_queue_full`
  - `prefetch_dropped_mshr_full`

### Step 6：增加 prefetch 去重与过滤

目标：

- 避免重复预取同一 block
- 避免对已经 in-cache 或 in-flight 的 line 重复发请求

建议修改位置：

- [`trace_sim/Cache.h`](/home/tututu/TraceSim/trace_sim/Cache.h)

最小过滤顺序：

1. 地址先对齐到 cache line
2. 若 block 已在 cache：
   - `prefetch_filtered_in_cache++`
   - 不发
3. 若 block 已在 `inflight`：
   - `prefetch_filtered_inflight++`
   - 不发
4. 若 prefetch queue 已经有同一个 block：
   - `prefetch_filtered_duplicate++`
   - 不发
5. 否则允许进入 prefetch queue

可选补充：

- 限制不跨 page：`(addr >> 12) == (base >> 12)`

为什么必须做：

- 没有去重，流量统计和 accuracy 都会失真

完成判据：

- 能打印：
  - `prefetch_filtered_in_cache`
  - `prefetch_filtered_inflight`
  - `prefetch_filtered_duplicate`

### Step 7：扩展 prefetcher 接口，支持 stride 类策略

目标：

- 支持 next-line
- 支持 PC-based stride

建议修改位置：

- [`trace_sim/Prefetcher.h`](/home/tututu/TraceSim/trace_sim/Prefetcher.h)

把接口从：

```cpp
void on_access(uint32_t addr, uint32_t line_size, bool hit, std::vector<uint32_t>& out)
```

改成：

```cpp
struct PrefetcherAccessInfo {
    uint32_t pc = 0;
    uint32_t addr = 0;
    uint32_t line_size = 0;
    bool is_load = false;
    bool is_store = false;
    bool hit = false;
    bool miss = false;
};

struct PrefetchRequest {
    uint32_t addr = 0;
};

virtual void on_access(
    const PrefetcherAccessInfo &info,
    std::vector<PrefetchRequest> &requests) = 0;
```

为什么必须做：

- `PC-based stride prefetcher` 没有 `PC` 无法训练
- 只靠地址和 hit/miss，很多策略都写不出来

完成判据：

- `NextLinePrefetcher` 可继续工作
- 新增一个 `PcStridePrefetcher` 原型

### Step 8：增加 useful / useless / late / coverage / accuracy 统计

目标：

- 让实验结果可解释

建议统计项：

- demand side
  - `demand_access`
  - `demand_miss`
  - `demand_miss_merged`
  - `demand_merged_into_prefetch`
- prefetch side
  - `prefetch_generated`
  - `prefetch_issued`
  - `prefetch_filtered`
  - `prefetch_dropped`
  - `useful_prefetch`
  - `useless_prefetch`
  - `late_prefetch`
- traffic side
  - `llc_read_access`
  - `dram_read_access`
  - `prefetch_memory_reads`

派生指标：

```text
prefetch_accuracy = useful_prefetch / prefetch_issued
prefetch_coverage = useful_prefetch / demand_miss_without_prefetch_or_current_miss_baseline
late_ratio = late_prefetch / demand_merged_into_prefetch_or_useful_prefetch
traffic_overhead = prefetch_memory_reads / total_memory_reads
```

`late prefetch` 的最小定义建议：

- 某个 demand 访问的目标 block 已经有一个 in-flight prefetch request
- 但 demand 到来时 line 还没 fill
- 这次 demand 只能 merge 等待，而不是立即 hit
- 记一次 `late_prefetch`

为什么必须做：

- 没有这组统计，预取器实验几乎无法解释

完成判据：

- 报告中不只输出 hit rate，还输出 accuracy / coverage / traffic / late

### Step 9：把 load/fetch 延迟统一改成由 memory system 返回

目标：

- 消除现在“现场判断 hit/miss 后手工累加 penalty”的简化写法

当前代码中的简化路径：

- load 侧：[`trace_sim/TraceSim.cpp`](/home/tututu/TraceSim/trace_sim/TraceSim.cpp#L189)
- fetch 侧：[`trace_sim/TraceSim.cpp`](/home/tututu/TraceSim/trace_sim/TraceSim.cpp#L345)

建议改法：

- `dcache.access()` / `icache.access()` 返回 `ready_cycle`
- CPU 只消费这个结果，不自行拼接 L1/LLC/DRAM penalty

为什么必须做：

- 这样 memory system 才是真正的时序权威来源

完成判据：

- load/fetch latency 逻辑从 `TraceSim.cpp` 移到 cache/memory 子系统

### Step 10：做最小验证

目标：

- 避免加完预取框架后统计口径是错的

建议验证用例：

1. 单一顺序流访问
   - next-line 应该高 useful，低 useless
2. 大步长跨行访问
   - next-line useful 低，traffic 增加
3. 重复访问同一 block
   - 不应重复发多个相同 prefetch
4. demand 紧跟 prefetch 之后
   - 应能观察到 `late_prefetch`
5. 小 cache + 激进预取
   - 应出现 `useless_prefetch` 和 pollution

建议输出：

- IPC
- demand miss
- prefetch issued
- useful/useless/late
- LLC/DRAM traffic

---

## 4. 建议的数据结构草图

### 4.1 Cache line

```cpp
struct Line {
    uint32_t tag = 0;
    bool valid = false;
    bool prefetched = false;
    bool used_by_demand = false;
    uint64_t last_access = 0;
    uint64_t insertion_time = 0;
    uint64_t fill_cycle = 0;
};
```

### 4.2 In-flight miss / MSHR

```cpp
struct InflightRequest {
    uint32_t block_addr = 0;
    uint64_t ready_cycle = 0;
    bool is_prefetch = false;
    bool has_demand_waiter = false;
    uint32_t source_pc = 0;
};
```

### 4.3 Prefetcher interface

```cpp
struct PrefetcherAccessInfo {
    uint32_t pc = 0;
    uint32_t addr = 0;
    uint32_t line_size = 0;
    bool is_load = false;
    bool is_store = false;
    bool hit = false;
    bool miss = false;
};

struct PrefetchRequest {
    uint32_t addr = 0;
};
```

---

## 5. 实现优先级

### P0：先补齐这些，不然实验结论不可信

1. line metadata：`prefetched / used_by_demand`
2. MSHR / in-flight miss table
3. return path：未来返回才 fill
4. demand/prefetch merge

### P1：补齐实验解释能力

1. prefetch queue / 节流 / MSHR 占用
2. demand 高优先级
3. useful / useless / late
4. filtered / dropped / traffic

### P2：补策略扩展能力

1. `PrefetcherAccessInfo` 增加 `PC`
2. 实现 `PcStridePrefetcher`
3. 加 page-boundary 规则

---

## 6. 最终目标：做到什么程度就够做“基础预取实验”

如果你完成了下面这份最小标准，就已经足够支持“基础且基本可信”的预取评估：

- cache line 带 prefetch metadata
- miss 不立即 fill，而是进入 in-flight
- 未来某个 cycle 返回后再 fill
- demand 和 prefetch 共用 MSHR / queue
- demand 可以 merge 到已有 prefetch request
- 有 useful/useless/late 统计
- 有 traffic 增量统计
- prefetcher 能拿到 `PC + addr + hit/miss + access type`

做到这里，你的 TraceSim 虽然还不是 ChampSim，但已经可以比较可靠地做：

- `no prefetch` vs `next-line`
- `next-line` vs `PC-based stride`
- prefetch degree、throttle、queue size 的敏感性分析

## 7. 不建议现在就做的事

下面这些可以以后再补，不是“基础实验”的门槛：

- 极细粒度 DRAM bank/channel 时序
- 多核共享 LLC 干扰
- TLB + page walker 全建模
- coherence
- 极复杂的 prefetch feedback controller

先把 P0/P1 做对，收益最高。
