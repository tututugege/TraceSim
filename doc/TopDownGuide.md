# TraceSim Top-down 归因说明

这份文档描述的是当前 `Profiler` 真正实现的归因口径。

## 1. 当前 bucket

当前归因桶定义在 [trace_sim/Profiler.h](/home/tututu/TraceSim/trace_sim/Profiler.h)：

- `Retiring`
- `Frontend Bound`
- `Memory Bound`
- `Core Bound`
- `Bad Speculation`
- `Other/Bubbles`

这里的 `Memory Bound` 和 `Core Bound` 是 `Backend Bound` 的二级拆分。

## 2. 当前每周期判定顺序

当前 `Profiler::mark_cycle()` 的优先级很直接：

1. 如果本周期有退休，记为 `Retiring`
2. 否则如果后端有 stall reason：
   - `MEMORY_WAIT` -> `Memory Bound`
   - 其他后端原因 -> `Core Bound`
3. 否则如果前端 stall：
   - `BRANCH_MISPREDICT` -> `Bad Speculation`
   - 其他前端原因 -> `Frontend Bound`
4. 否则 -> `Other/Bubbles`

这意味着当前模型采用的是“单桶归因”，每个周期只进一个桶。

## 3. 当前前端归因

前端相关原因来自 `FetchStallReason`。当前会影响归因的典型来源包括：

- `ICACHE_MISS`
- `LINE_BOUNDARY`
- `FETCH_REDIRECT`
- `BRANCH_MISPREDICT`

其中：

- `BRANCH_MISPREDICT` 归到 `Bad Speculation`
- 其余前端 stall 归到 `Frontend Bound`

所以当前模型已经把“误预测气泡”和“其他前端供应不足”分开了。

## 4. 当前后端归因

后端 stall reason 主要来自：

- `dispatch_stage()`
  - `ROB_FULL`
  - `ALU_IQ_FULL`
  - `LDU_IQ_FULL`
  - `STA_IQ_FULL`
  - `STD_IQ_FULL`
  - `BRU_IQ_FULL`
  - `TRAP_SERIALIZE`
- `classify_commit_stall()`
  - `MEMORY_WAIT`
  - `EXEC_WAIT`

当前最关键的 simplification 是：

- 如果 ROB head 是 load 且 `waiting_on_memory`，就归到 `Memory Bound`
- 其他 head stall 或资源类 stall，都归到 `Core Bound`

这和你之前想要的“尽量按 ROB head 来看 memory bound”是一致的。

## 5. 当前模型的意义和局限

这套实现适合做：

- 宏观瓶颈定位
- 对比不同 BPU / prefetcher / cache policy 时的方向性变化
- 判断性能变化主要来自前端、内存还是核心资源

但它不是 Intel 原版 top-down 的严格复制。它没有做例如：

- 重叠归因
- 更细的 FE latency / FE bandwidth 拆分
- memory latency / memory bandwidth 的进一步拆分
- allocator、rename、scheduler 等更细的 core bound 子类

## 6. 如何解读当前输出

一个比较实用的读法是：

- `Retiring` 高：整体吞吐还可以
- `Bad Speculation` 高：优先看 BPU
- `Frontend Bound` 高：优先看 I$、fetch 宽度、redirect、inst buffer
- `Memory Bound` 高：优先看 D$ / LLC / prefetch / miss latency
- `Core Bound` 高：优先看 ROB / IQ / FU 数量 / 数据依赖
- `Other/Bubbles` 高：说明还有模型空洞，或者流水线存在未细分的气泡

## 7. 当前最适合继续补的方向

如果后面要继续细化 top-down，比较自然的顺序是：

1. 细化 `Frontend Bound`
   - 比如区分 `I-Cache miss`、`redirect`、`line boundary`
2. 细化 `Core Bound`
   - 比如区分不同 IQ full、ROB full、other
3. 细化 `Memory Bound`
   - 比如区分 L1/L2/DRAM 或 late prefetch 影响

但在当前阶段，这些都属于“精修”，不是基础功能缺失。
