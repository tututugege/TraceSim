#include "ref.h"
#include "CSR.h"
#include <cstdint>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <zlib.h>

// --- 辅助函数：简化 zlib 读写 POD 类型 ---
template <typename T> void gz_write_pod(gzFile file, const T &data) {
  if (gzwrite(file, &data, sizeof(T)) != sizeof(T)) {
    std::cerr << "Error writing data to gzip file." << std::endl;
    exit(1);
  }
}

template <typename T> void gz_read_pod(gzFile file, T &data) {
  if (gzread(file, &data, sizeof(T)) != sizeof(T)) {
    std::cerr << "Error reading data from gzip file." << std::endl;
    exit(1);
  }
}



#include <iostream>
#include <string>
#include <zlib.h>

// 单次写入的安全块大小 (16MB)
const uint64_t GZ_CHUNK_SIZE = 16ULL * 1024 * 1024;



void Ref_cpu::restore_checkpoint(const std::string &filename) {
  std::string final_name = filename;
  gzFile file = gzopen(final_name.c_str(), "rb");
  if (!file && final_name.find(".gz") == std::string::npos) {
    final_name += ".gz";
    file = gzopen(final_name.c_str(), "rb");
  }

  if (!file) {
    std::cerr << "Error: Could not open file: " << filename << std::endl;
    exit(1);
  }

  // 1. 恢复状态
  gz_read_pod(file, state);
  uint64_t interval_inst_count;
  gz_read_pod(file, interval_inst_count);

  privilege = 0; // RISCV_MODE_U (参考实现)
  sim_time = interval_inst_count;

  // 2. 恢复内存
  if (memory == nullptr) {
    std::cerr << "Error: Memory not allocated." << std::endl;
    exit(1);
  }

  // [关键] 完整 Checkpoint 包含 ram_size * 4 字节
  uint64_t total_bytes = (uint64_t)ram_size * sizeof(uint32_t);
  uint8_t *byte_ptr = reinterpret_cast<uint8_t *>(memory);
  uint64_t remain = total_bytes;
  uint64_t read_total = 0;

  std::cout << "Restoring Memory (Target: " << total_bytes << " bytes)..." << std::endl;

  while (remain > 0) {
    unsigned int chunk = (remain > GZ_CHUNK_SIZE) ? (unsigned int)GZ_CHUNK_SIZE
                                                  : (unsigned int)remain;

    int read_bytes = gzread(file, byte_ptr, chunk);
    if (read_bytes < 0) {
      std::cerr << "Error: gzread failed." << std::endl;
      exit(1);
    }
    if (read_bytes == 0) {
      std::cout << "Warning: Unexpected EOF after reading " << read_total << " bytes." << std::endl;
      break; 
    }

    byte_ptr += read_bytes;
    remain -= read_bytes;
    read_total += read_bytes;
  }

  gzclose(file);
  std::cout << "Checkpoint restored: PC=0x" << std::hex << state.pc 
            << ", satp=0x" << state.csr[cvt_number_to_csr(number_satp)] 
            << ", Total Memory Read=" << std::dec << read_total << " bytes" << std::endl;
}