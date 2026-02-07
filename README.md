# TraceSim - RISC-V Trace-based OoO Simulator

TraceSim 是一个高性能的 RISC-V 循环精确（Cycle-accurate）乱序执行（Out-of-Order）仿真器。它采用“在线追踪（Online Trace）”架构，由一个功能模型（Ref_cpu）作为指令供应器，直接向时序模型（TraceSim）提供执行流，从而避免了生成巨型 Trace 文件的 I/O 开销。

## 核心特性

- **极速仿真**: 经过深度优化（循环 ROB、索引 IQ、Store 追踪、FU 预计算），在 Dhrystone 上可达 **17.5 MIPS** (提速 4.5x)。
- **指令供应器 (Instruction Supplier)**: 重构后的 `Ref_cpu` 支持单步执行并导出详细指令包（`TraceInst`）。
- **乱序流水线**: 完整实现了包含取指、分发、发射、执行、提交的流水线逻辑。
- **资源建模**:
    - **ROB (Reorder Buffer)**: 128 条记录，基于 **Circular Buffer** 实现，支持 $O(1)$ 访问。
    - **分布式发射队列 (Distributed IQ)**: 分别为 ALU, LDU, STA, STD, BRU 设置独立队列。
    - **后端宽度**: 4-wide。
    - **功能单元 (FU)**: ALU (4), LDU (1), STA (1), STD (1), BRU (1)。
- **存储层次模拟**:
    - **I-Cache/D-Cache**: 独立模拟，支持配置容量、组相联度及失效惩罚。
- **先进预测与访存**:
    - **可插拔分支预测**: 支持 Gshare 及随机预测器。
    - **指令裂变 (Cracking)**: Store 指令自动裂变为 STA (地址) 和 STD (数据) 微操作。
    - **存储转发 (STLF)**: 支持从未提交的 Store 直接前传数据给 Load，采用高效的 Store 追踪机制。

## 运行模式及用法

二进制文件名为 `a.out`，支持以下三种运行模式：

### 1. 乱序仿真模式 (TRACE)
运行完整的乱序流水线仿真，适用于从头开始跑程序。
- **无 Warmup**：直接进入采样阶段。
- **运行长度**：通过 `--max-insts` 指定。

```bash
./a.out --mode trace --image ./test/dhrystone.bin --max-insts 10000000
```

### 2. 快照恢复仿真模式 (RESTORE)
从 Checkpoint 文件恢复状态并进行 OoO 仿真，专门用于 SimPoint 采样。
- **有 Warmup**：默认先运行 1 亿条指令进行预热（配置见 `SimConfig.h`）。
- **自动退出**：完成 1 亿条 Warmup 和 1 亿条 Sample 后**强制自动退出**。

```bash
./a.out --mode restore --restore-file ./test/ckpt.gz
```

### 3. 普通功能仿真模式 (NORMAL)
仅运行 RISC-V 指令功能核，不进入乱序时序模型。

```bash
./a.out --mode normal --image ./test/dhrystone.bin
```

## 参数说明

- `--mode <normal|trace|restore>`: 选择运行模式。
- `--image <path>`: 指定加载的裸机二进制镜像（.bin）。
- `--restore-file <path>`: 指定加载的 Checkpoint 文件（.gz）。
- `--max-insts <N>`: 设置 TRACE 模式下的最大执行指令数。

## 开发与文档
详细的优化细节和验证结果请参考 [walkthrough.md](file:///home/tututu/.gemini/antigravity/brain/d9acf1c2-511f-413c-a91d-caf3a90d97d8/walkthrough.md)。
