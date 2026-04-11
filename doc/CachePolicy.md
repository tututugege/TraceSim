# TraceSim Cache 与替换策略说明

这份文档描述当前 cache 本体和替换策略接口。

## 1. 当前 cache 不是纯延迟函数

当前 cache 实现在 [trace_sim/mem/Cache.h](/home/tututu/TraceSim/trace_sim/mem/Cache.h)。

它已经维护了真实状态：

- set / way
- tag
- valid
- line metadata
- in-flight request table

因此当前 cache fill、eviction、merge 都会真正修改状态，而不是简单返回一个固定延迟。

## 2. 当前 line metadata

`Cache::Line` 当前至少包含：

- `tag`
- `valid`
- `prefetched`
- `used_by_demand`
- `last_access`
- `insertion_time`
- `fill_cycle`

这些字段分别支持：

- 基本 tag match
- replacement state 更新
- useful / useless prefetch 统计
- 最近访问和插入时间跟踪

## 3. 当前替换策略接口

接口在 [trace_sim/mem/ReplacementPolicy.h](/home/tututu/TraceSim/trace_sim/mem/ReplacementPolicy.h)。

核心接口如下：

```cpp
class CacheReplacementPolicy {
public:
    struct LineView {
        bool valid = false;
        uint32_t tag = 0;
        uint64_t last_access = 0;
        uint64_t insertion_time = 0;
    };

    virtual ~CacheReplacementPolicy() = default;
    virtual const char *name() const = 0;
    virtual size_t choose_victim(
        uint32_t set_index,
        const std::vector<LineView> &set_lines) const = 0;
    virtual void on_cache_hit(
        uint32_t set_index,
        size_t way,
        uint64_t cycle,
        std::vector<LineView> &set_lines) const = 0;
    virtual void on_cache_fill(
        uint32_t set_index,
        size_t way,
        uint64_t cycle,
        std::vector<LineView> &set_lines) const = 0;
};
```

当前内置：

- `LRU`
- `FIFO`

## 4. 当前替换流程

当 cache fill 发生时，流程大致是：

1. 根据地址找到 set
2. 构造当前 set 的 `LineView`
3. 调用 `choose_victim()`
4. 如 victim 是未被 demand 使用过的 prefetched line，统计 `useless_prefetch`
5. 安装新 line
6. 调用 `on_cache_fill()` 更新 replacement metadata

当 cache hit 时：

1. 找到命中的 way
2. 调用 `on_cache_hit()`
3. 把更新后的 `LineView` 写回真实 line 状态

## 5. 当前设计的取舍

这个接口的优点是：

- 简单
- 可插拔
- 对做算法原型验证足够快

它的限制也很明确：

- policy 目前只能看到 `LineView`
- 还看不到更丰富的上下文，比如 PC、prefetch 来源、reuse hint
- 还没有专门的 prefetch-aware insertion / bypass 接口

所以如果你要做 RRIP、DIP、Hawkeye 这一类更复杂的策略，后面大概率还要再扩展接口。

## 6. 如何加自己的替换策略

最简单的方式是：

1. 在 [trace_sim/mem/ReplacementPolicy.h](/home/tututu/TraceSim/trace_sim/mem/ReplacementPolicy.h) 里复制 `TemplateReplacementPolicy`
2. 实现自己的 victim 选择和 metadata 更新
3. 在 `ReplacementPolicyType` 里加一个新枚举
4. 在 `make_replacement_policy()` 里加一个 `case`
5. 在 [trace_sim/SimConfig.h](/home/tututu/TraceSim/trace_sim/SimConfig.h) 里切 `ICACHE_REPLACEMENT`、`DCACHE_REPLACEMENT` 或 `LLC_REPLACEMENT`
