# TraceSim 分支预测说明

这份文档描述当前仓库里已经实现的分支预测接口和行为。

## 1. 当前接口

分支预测器接口在 [trace_sim/frontend/BranchPredictor.h](/home/tututu/TraceSim/trace_sim/frontend/BranchPredictor.h)。

核心接口如下：

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

设计意图是：

- `predict()` 只负责预测
- `update()` 只负责训练
- `meta` 用来把预测阶段查出来的索引或中间状态带到训练阶段，避免重复查表

## 2. 当前内置模式

当前配置枚举在 [trace_sim/SimConfig.h](/home/tututu/TraceSim/trace_sim/SimConfig.h)。

已支持：

- `GSHARE`
- `ALWAYS_TAKEN`
- `ALWAYS_NOT_TAKEN`
- `PROBABILISTIC`
- `PERFECT`

其中前 3 个是“真实 predictor 对象”。

后 2 个是 oracle 模式：

- `PROBABILISTIC`
  - 按 `BP_TARGET_ACCURACY` 概率决定是否预测正确
- `PERFECT`
  - 永远返回真实方向

这两种模式主要用于做上界或敏感性分析。

## 3. 预测发生在什么时候

当前预测发生在前端取指时，也就是 `Frontend::fetch_stage()` 里。

大致流程是：

1. 前端取到一条 branch
2. 调用 BPU 做方向预测
3. 立刻用 trace 里的真实结果判断是否预测正确
4. 若使用真实 predictor，则调用 `update()`
5. 若预测错误，设置 `BRANCH_MISPREDICT_PENALTY`
6. 若预测正确但分支被 taken，则设置 `FETCH_REDIRECT_LATENCY`

这意味着当前模型更接近“fetch-time training”的简化实现，而不是“执行后才 resolve 再 update”的严格硬件时序。

## 4. 当前惩罚模型

当前有两类主要分支相关代价：

- `FETCH_REDIRECT_LATENCY`
  - 正确预测的 taken branch / jump 的 redirect 代价
- `BRANCH_MISPREDICT_PENALTY`
  - 误预测导致的前端停顿代价

这两个参数都定义在 [trace_sim/SimConfig.h](/home/tututu/TraceSim/trace_sim/SimConfig.h)。

## 5. 当前实现边界

当前 BPU 模型已经足够支持：

- 静态 predictor 基线
- GShare 这类基础动态 predictor
- 完美预测 / 指定准确率上界实验

但它还没有实现：

- BTB 容量与冲突
- target prediction 细节
- RAS
- 多级 predictor 组合逻辑
- 真正的 execute-time update 延迟

所以如果你要研究非常细的前端时序，这个模型还需要继续扩展。

## 6. 如何加自己的 predictor

最简单的方式是：

1. 在 [trace_sim/frontend/BranchPredictor.h](/home/tututu/TraceSim/trace_sim/frontend/BranchPredictor.h) 里复制 `TemplateBranchPredictor`
2. 改名并实现自己的 `predict()` / `update()`
3. 在 `BP_Type` 里加一个新枚举
4. 在 `make_branch_predictor()` 工厂里加一个 `case`
5. 在 [trace_sim/SimConfig.h](/home/tututu/TraceSim/trace_sim/SimConfig.h) 里把 `BP_TYPE` 切过去

如果只是想测上界，不需要新类，直接用 `PERFECT` 即可。
