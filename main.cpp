#include "RISCV.h"
#include "ref.h"
#include "trace_sim/TraceSim.h"
#include <cstdlib>

Ref_cpu ref_cpu;
int main(int argc, char *argv[]) {
  SimConfig config;
  std::string positional_input;

  // 简易参数解析
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--image" && i + 1 < argc)
      config.image_file = argv[++i];

    // 模式选择
    else if (arg == "--mode" && i + 1 < argc) {
      std::string m = argv[++i];
      if (m == "normal")
        config.mode = SimMode::NORMAL;
      else if (m == "trace")
        config.mode = SimMode::TRACE;
      else if (m == "ckpt")
        config.mode = SimMode::RESTORE;
    }

    // 路径参数

    else if (arg == "--restore-file" && i + 1 < argc)
      config.restore_file = argv[++i];
    else if (arg == "--max-insts" && i + 1 < argc)
      config.max_insts = std::stoull(argv[++i]);
    else if (arg == "--enable-dependent-load-profiling")
      config.enable_dependent_load_profiling = true;
    else if (arg == "--disable-dependent-load-profiling")
      config.enable_dependent_load_profiling = false;
    else if (!arg.empty() && arg[0] != '-') {
      positional_input = arg;
    }
  }

  // Positional fallback:
  // ckpt mode -> checkpoint file, otherwise -> image file.
  if (!positional_input.empty()) {
    if (config.mode == SimMode::RESTORE) {
      if (config.restore_file.empty())
        config.restore_file = positional_input;
    } else {
      if (config.image_file.empty())
        config.image_file = positional_input;
    }
  }

  ref_cpu.init(0, config.image_file.c_str(), PHYSICAL_MEMORY_LENGTH);
  
  if (!config.restore_file.empty()) {
    ref_cpu.restore_checkpoint(config.restore_file);
  }

  if (config.mode == SimMode::TRACE || config.mode == SimMode::RESTORE) {
    TraceSim trace_sim(ref_cpu, 
                       config.mode,
                       TraceSimConfig::FETCH_WIDTH, 
                       TraceSimConfig::ROB_SIZE, 
                       TraceSimConfig::ALU_IQ_SIZE,
                       TraceSimConfig::LDU_IQ_SIZE,
                       TraceSimConfig::STA_IQ_SIZE,
                       TraceSimConfig::STD_IQ_SIZE,
                       TraceSimConfig::BRU_IQ_SIZE,
                       config.max_insts,
                       config.enable_dependent_load_profiling);
    trace_sim.run();
  } else {
    ref_cpu.exec(config);
  }

  return 0;
}
