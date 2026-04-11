# TraceSim

TraceSim 是一个面向研究用途的 trace-driven、乱序 CPU/内存模拟器。当前版本重点支持三类实验：

- 分支预测器
- Cache 预取器
- Cache 替换策略

它不是 gem5 或 ChampSim 的完整替代品，但已经具备做基础性能归因和基础预取评估所需的最小骨架：有状态 cache、在途 miss 跟踪、未来回填、可插拔组件接口，以及简化版 top-down 归因。

## 当前架构

当前代码大致分成四块：

- `Frontend`
  - 负责从 trace/ref model 取指
  - 做 I-Cache 访问
  - 做分支预测与 redirect / mispredict stall
- `TraceSim`
  - 负责主循环
  - 维护 ROB、IQ、寄存器 ready time
  - 串起前端、后端、Profiler、MemSubsystem
- `LoadStoreUnit`
  - 负责 load/store 发射
  - 处理 store dependence 与 STLF
  - 通过 `MemSubsystem` 发起 D-Cache 访问
- `MemSubsystem`
  - 统一管理 I-Cache / D-Cache / LLC
  - 管理 prefetch queue
  - 处理 request / merge / future return / fill

## 当前已经支持的研究接口

### 1. Branch Predictor

接口文件：

- [trace_sim/frontend/BranchPredictor.h](/home/tututu/TraceSim/trace_sim/frontend/BranchPredictor.h)

当前内置：

- `GSHARE`
- `ALWAYS_TAKEN`
- `ALWAYS_NOT_TAKEN`
- `PROBABILISTIC`
- `PERFECT`

其中：

- `PROBABILISTIC`
- `PERFECT`

属于 oracle 模式，不走真实 BPU 对象。

### 2. Prefetcher

接口文件：

- [trace_sim/mem/Prefetcher.h](/home/tututu/TraceSim/trace_sim/mem/Prefetcher.h)

当前内置：

- `NONE`
- `NEXT_LINE`
- `PC_STRIDE`

预取器只生成候选地址，真正能不能发出、是否被过滤、是否会占用在途资源，交给 `MemSubsystem` 和 `Cache` 决定。

### 3. Replacement Policy

接口文件：

- [trace_sim/mem/ReplacementPolicy.h](/home/tututu/TraceSim/trace_sim/mem/ReplacementPolicy.h)

当前内置：

- `LRU`
- `FIFO`

## 当前内存系统能力

当前的存储层次已经不是“纯 hit/miss 固定延迟函数”，而是有状态模型：

- 维护 set / way / tag / valid
- 维护 replacement metadata
- 维护 in-flight request table
- 支持 demand miss merge
- 支持 prefetch request 走正常 request/fill 路径
- line 返回前不会被错误当成 hit
- 支持 useful / useless / late prefetch 这类基础统计

当前模型仍然是简化版，不等同于完整的现代处理器内存系统。比如现在还没有：

- 独立的 prefetch MSHR 配额
- 多级 prefetch 协同策略
- 很细的 bandwidth / port arbitration

## Top-down 归因

当前 `Profiler` 输出的桶包括：

- `Retiring`
- `Bad Speculation`
- `Frontend Bound`
- `Memory Bound`
- `Core Bound`
- `Other/Bubbles`

目前的核心口径是：

- 只要该周期有退休，归到 `Retiring`
- 如果 ROB head 因 load 等内存，归到 `Memory Bound`
- 其他后端停顿归到 `Core Bound`
- 前端 stall 且原因是误预测，归到 `Bad Speculation`
- 其他前端 stall 归到 `Frontend Bound`

这是一个有意简化过的、适合当前 trace 模型的实现，不是 Intel 原版 top-down 的完全复刻。

## 编译与运行

编译：

```bash
make -j4
```

运行一个 trace 模式样例：

```bash
./a.out --mode trace binary/parser-125k.bin --max-insts 200000
```

如果只想快速看默认配置下的统计，改 [trace_sim/SimConfig.h](/home/tututu/TraceSim/trace_sim/SimConfig.h) 后重新编译即可。

## 建议阅读顺序

- 架构总览：[doc/Architecture.md](/home/tututu/TraceSim/doc/Architecture.md)
- 分支预测：[doc/BranchPrediction.md](/home/tututu/TraceSim/doc/BranchPrediction.md)
- 预取实现：[doc/Prefetching.md](/home/tututu/TraceSim/doc/Prefetching.md)
- 替换策略：[doc/CachePolicy.md](/home/tututu/TraceSim/doc/CachePolicy.md)
- Top-down 归因：[doc/TopDownGuide.md](/home/tututu/TraceSim/doc/TopDownGuide.md)
- 扩展接口：[doc/EXTENDING_COMPONENTS.md](/home/tututu/TraceSim/doc/EXTENDING_COMPONENTS.md)
