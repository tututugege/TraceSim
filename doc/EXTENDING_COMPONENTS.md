# Extending Research Components

这份文档只回答一个问题：如果你想快速验证一个自己的 idea，应该改哪里。

当前支持三类可插拔组件：

- Branch Predictor
- Cache Prefetcher
- Cache Replacement Policy

目标是把“新增一个组件”的改动尽量收敛成：

1. 写一个新类
2. 在对应工厂里加一个 case
3. 在 `SimConfig.h` 里切配置

## 1. Branch Predictor

相关文件：

- [trace_sim/frontend/BranchPredictor.h](/home/tututu/TraceSim/trace_sim/frontend/BranchPredictor.h)
- [trace_sim/SimConfig.h](/home/tututu/TraceSim/trace_sim/SimConfig.h)

### 接口

你需要继承：

```cpp
class BranchPredictor {
public:
    struct Prediction {
        bool taken = false;
        uint64_t meta = 0;
    };

    virtual ~BranchPredictor() = default;
    virtual const char *name() const = 0;
    virtual Prediction predict(uint32_t pc) = 0;
    virtual void update(uint32_t pc, bool actual_taken, uint64_t meta) = 0;
};
```

### 最小接入步骤

1. 在 `BranchPredictor.h` 里新增你的类
2. 在 `make_branch_predictor(TraceSimConfig::BP_Type type)` 里加一个 `case`
3. 在 `SimConfig.h` 的 `BP_Type` 枚举里加一个值
4. 把 `BP_TYPE` 切到你的实现

### 模板

头文件里已经放了一个 `TemplateBranchPredictor`，可以直接复制改名。

设计意图是：

- `predict()` 只做预测，并返回一个 `Prediction`
- `Prediction.meta` 用来保存你在训练时还想再用一次的索引或中间状态
- `update()` 用真实结果训练，不需要再次重复查表

### 备注

`PERFECT` 和 `PROBABILISTIC` 这两种模式现在属于 oracle 模式，不走真实 BPU 对象。  
如果你要写自己的 BPU，请用普通枚举项 + `make_branch_predictor()` 工厂接入。

## 2. Prefetcher

相关文件：

- [trace_sim/mem/Prefetcher.h](/home/tututu/TraceSim/trace_sim/mem/Prefetcher.h)
- [trace_sim/SimConfig.h](/home/tututu/TraceSim/trace_sim/SimConfig.h)
- [trace_sim/mem/MemSubsystem.cpp](/home/tututu/TraceSim/trace_sim/mem/MemSubsystem.cpp)

### 接口

你需要继承：

```cpp
class CachePrefetcher {
public:
    virtual ~CachePrefetcher() = default;
    virtual const char *name() const = 0;
    virtual void on_access(const PrefetcherAccessInfo &info,
                           std::vector<PrefetchRequest> &requests) = 0;
};
```

输入是一次 demand 访问的上下文，输出是候选预取地址列表。

### 最小接入步骤

1. 在 `Prefetcher.h` 里新增你的 prefetcher 类
2. 在 `make_prefetcher(TraceSimConfig::PrefetcherType type)` 里加一个 `case`
3. 在 `SimConfig.h` 的 `PrefetcherType` 枚举里加一个值
4. 把 `ICACHE_PREFETCHER` / `DCACHE_PREFETCHER` / `LLC_PREFETCHER` 切到你的实现

### 当前优势

现在 prefetcher 的接入点已经统一到 `MemSubsystem`，不需要你自己再去碰 cache fill / queue / MSHR 路径。

### 模板

头文件里已经放了一个 `TemplatePrefetcher`，可以直接复制改名。  
它只做一件事：根据一次 demand 访问，生成候选预取地址；真正发不发、能不能进 cache，还是由 `MemSubsystem` 决定。

## 3. Replacement Policy

相关文件：

- [trace_sim/mem/ReplacementPolicy.h](/home/tututu/TraceSim/trace_sim/mem/ReplacementPolicy.h)
- [trace_sim/SimConfig.h](/home/tututu/TraceSim/trace_sim/SimConfig.h)
- [trace_sim/mem/MemSubsystem.cpp](/home/tututu/TraceSim/trace_sim/mem/MemSubsystem.cpp)

### 接口

你需要继承：

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

### 最小接入步骤

1. 在 `ReplacementPolicy.h` 里新增你的 policy 类
2. 在 `make_replacement_policy(TraceSimConfig::ReplacementPolicyType type)` 里加一个 `case`
3. 在 `SimConfig.h` 的 `ReplacementPolicyType` 枚举里加一个值
4. 把 `ICACHE_REPLACEMENT` / `DCACHE_REPLACEMENT` / `LLC_REPLACEMENT` 切到你的实现

### 模板

头文件里已经放了一个 `TemplateReplacementPolicy`，可以直接复制改名。  
它把三个最关键的入口都留出来了：

- `choose_victim()` 负责选 victim
- `on_cache_hit()` 负责 hit 时更新元数据
- `on_cache_fill()` 负责 fill 时更新元数据

## 4. 当前最适合做实验的方式

如果你想快速验证自己的 idea，建议遵循下面的节奏：

1. 先只在一个组件里改
2. 先只切 `D$ prefetcher` 或 `BPU` 这种单点
3. 不要一开始同时改 prefetcher 和 replacement policy
4. 先保证 baseline 和你的新实现只差一个配置项

这样更容易确认性能变化到底来自哪个模块。
