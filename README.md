# TraceSim: 高性能乱序 RISC-V 归因仿真器

TraceSim 是一个模块化、循环精确（Cycle-accurate）的乱序执行（Out-of-Order）仿真器。它旨在为计算机体系结构研究提供一个既能快速迭代又能深度量化性能瓶颈（Top-down Analysis）的实验平台。

## 🚀 核心架构演进 (Decoupled Design)

TraceSim 采用了深度的模块化解耦设计，模拟了现代处理器的物理结构：

- **Frontend (前端)**: 负责取指、I-Cache 访问、对齐缓冲以及分支预测。
- **Execution Backend (执行后端)**: 包含分发（Dispatch）、分布式发射队列（Issue Queues）以及重排序缓存（ROB）。
- **LoadStoreUnit (LSU)**: 独立建模的访存子系统，支持复杂的存储检索、Store-to-Load 前传（STLF）及 D-Cache 交互。
- **Profiler (归因引擎)**: 核心解耦组件，负责采集流水线所有阶段的阻塞信号并生成 Top-down 矩阵。

### 三级带宽模型
为了模拟真实流水线，系统支持独立的带宽配置：
- **Fetch Width**: 取指带宽，用于填充指令缓冲区。
- **Dispatch Width**: 核心重命名与分发宽度。
- **Commit Width**: 退休带宽，决定最终 IPC 的上限。

## 📊 高精度归因系统 (Top-down Analysis)

TraceSim 内置了先进的归因引擎，能自动将执行周期划分为以下维度：

- **Retiring**: 指令成功退休，代表有效性能。
- **Frontend Bound**: 前端供应不足（Cache Miss, Mispredict stalls）。
- **Backend Bound**:
    - **Memory Bound**: 后端在等待数据回填。
    - **Core Bound**: 后端由于数据依赖（Data-flow）或执行单元延迟而停顿。
- **Bad Speculation**: 分支预测错误导致的流水线刷新损失。

### 报告示例 (Dhrystone)
```text
Top-Down Attribution Matrix:
  [Retiring            ]:   79.0% (有效执行)
  [Core Bound          ]:   17.4% (数据依赖主导)
  [Frontend Bound      ]:    2.6% (指令缓存/对齐)
  [Memory Bound        ]:    0.8% (访存压力)
  [Bad Speculation     ]:    0.1% (预测准确率 99.9%)
```

## 🛠 快速上手

### 编译
```bash
make -j
```

### 运行乱序仿真 (TRACE 模式)
```bash
./a.out --mode trace ./binary/dhrystone.bin --max-insts 10000000
```

### 核心参数调节 (`trace_sim/SimConfig.h`)
- `ROB_SIZE`: 默认为 256 (符合主流 OOO 核心)。
- `DISPATCH_WIDTH`: 默认为 6。
- `LDU_LATENCY`: 存储访问基本延迟。

## 📚 详细技术手册
- **核心原理**: [系统架构与子模块职责](doc/Architecture.md) | [Top-down 归因判据指南](doc/TopDownGuide.md)
- **专题研究**: [存储层次与替换策略](doc/CachePolicy.md) | [分支预测 (BPU) 研究](doc/BranchPrediction.md) | [预取器实验手册](doc/Prefetching.md)

---
> **Note**: 本模拟器支持 bit-exact 验证，确保在高性能乱序执行的同时，结果与指令集功能模型（Ref_cpu）完全一致。
