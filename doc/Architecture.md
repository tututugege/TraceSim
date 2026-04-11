# TraceSim 架构总览

这份文档只描述当前仓库里已经实现的结构，不讨论理想化设计。

## 1. 模块划分

当前核心模块如下：

- `Frontend`
  - 从 `Ref_cpu` 取 trace 指令
  - 做 I-Cache 访问
  - 处理 line boundary、fetch redirect、branch mispredict
- `TraceSim`
  - 维护主循环
  - 维护 ROB、各类 IQ、寄存器 ready time
  - 负责 commit / dispatch / issue 调度
- `LoadStoreUnit`
  - 处理 load/store 发射
  - 处理 store dependence 和 STLF
  - 通过 `MemSubsystem` 做 D-Cache 访问
- `MemSubsystem`
  - 统一管理 I-Cache / D-Cache / LLC
  - 统一管理 prefetch queue
  - 统一处理 request、merge、return、fill、统计
- `Profiler`
  - 每周期做 top-down 归因
  - 输出 branch / memory / top-down 统计

## 2. 数据流

可以把当前流水线理解成下面这个路径：

```text
Ref_cpu
  -> Frontend
  -> inst_buffer
  -> fetch_queue
  -> ROB + IQ
  -> issue / execute
  -> commit
```

访存相关路径则是：

```text
Frontend / LSU
  -> MemSubsystem
  -> L1 I$ / D$
  -> LLC
  -> future return
  -> cache fill
```

## 3. 每周期主循环

当前 `TraceSim::run()` 的顺序大致是：

1. `mem->process_returns(total_cycles)`
2. `advance_cycle()`
3. `commit_stage()`
4. `issue_stage()`
5. `dispatch_stage()`
6. `decode_stage()`
7. `frontend->fetch_stage()`
8. `mem->service_prefetch_queues(total_cycles)`
9. `profiler->mark_cycle(...)`

这说明两件事：

- cache line 的返回是显式建模的，不是 miss 当场生效
- prefetch 也不是生成后立刻生效，而是先进 queue，再在周期末尝试发出

## 4. 前端模型

当前前端已经有几个关键特性：

- `FETCH_WIDTH` 和 `DISPATCH_WIDTH` 分离
- 有 `inst_buffer` 做前后端解耦
- I-Cache 按 cache line 建模
- 跨 line 会产生 boundary 停顿
- taken branch / jump 会产生 redirect stall
- mispredict 会产生固定 penalty

当前仍然是简化模型，没有实现例如：

- BTB 容量建模
- uop cache
- 多级取指队列
- 复杂的 fetch packet 切分

## 5. 后端模型

当前后端由：

- ROB
- ALU IQ
- LDU IQ
- STA IQ
- STD IQ
- BRU IQ

构成。

指令进入 ROB 后，会根据功能类型进入相应 IQ。发射时按寄存器 ready time 和功能单元数量判断是否可以 issue。

当前后端的重点是：

- 支持基本乱序
- 支持不同类型 IQ 的容量限制
- 支持 ROB head 的 stall 归因

它不是一个精细的物理时序后端，没有实现例如：

- wakeup/select 详细时序
- scheduler bank conflict
- rename map / free list 细节
- 真实的 replay 机制

## 6. 访存模型

当前访存路径通过 `MemSubsystem` 统一管理。它内部包含：

- `icache`
- `dcache`
- `llc`
- I\$/D\$ prefetch queue

当前 cache 已经具备：

- set-associative 状态
- replacement metadata
- in-flight miss 跟踪
- demand / prefetch merge
- future return fill
- prefetched / used_by_demand 统计

这是当前做基础 prefetch 实验的关键基础。

## 7. 当前最值得继续扩展的方向

如果接下来要继续做研究功能，优先级通常是：

1. 扩展 BPU / prefetcher / replacement policy
2. 补更多 prefetch 指标
3. 细化前端模型
4. 细化内存带宽与资源竞争

对应的扩展入口见 [EXTENDING_COMPONENTS.md](/home/tututu/TraceSim/doc/EXTENDING_COMPONENTS.md)。
