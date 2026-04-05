#include "CSR.h"
#include "ref.h"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>
#include <zlib.h>

namespace {
constexpr uint32_t CKPT_MAGIC = 0x006d6552u; // "Rem\0" (little-endian)
constexpr uint32_t CKPT_VERSION = 2u;
constexpr uint32_t BOOT_IO_BASE = 0x00000000u;
constexpr uint32_t BOOT_IO_SIZE = 0x00002000u;
constexpr uint32_t LEGACY_TIMER_MMIO_SIZE = 0x8u;

struct CkptHeader {
  uint32_t magic;
  uint32_t version;
  uint32_t ram_size;
  uint32_t io_range_count;
};

struct CkptIoRange {
  uint32_t base;
  uint32_t size;
};

struct SimpointCpuStateV2 {
  uint32_t gpr[32];
  uint32_t csr[21];
  uint32_t pc;
  uint32_t store_addr;
  uint32_t store_data;
  uint32_t store_strb;
  bool store;
  bool reserve_valid;
  uint32_t reserve_addr;
};

std::vector<CkptIoRange> expected_io_layout() {
  return {
      {BOOT_IO_BASE, BOOT_IO_SIZE},
      {UART_BASE, UART_MMIO_SIZE},
      {PLIC_BASE, PLIC_MMIO_SIZE},
      {TIMER_BASE, LEGACY_TIMER_MMIO_SIZE},
  };
}

template <typename T> void gz_read_pod(gzFile file, T &data) {
  if (gzread(file, &data, sizeof(T)) != sizeof(T)) {
    std::cerr << "Error reading checkpoint data." << std::endl;
    exit(1);
  }
}

void gz_read_bytes(gzFile file, void *buf, size_t len) {
  if (len == 0) {
    return;
  }
  if (gzread(file, buf, static_cast<unsigned int>(len)) != static_cast<int>(len)) {
    std::cerr << "Error reading checkpoint payload." << std::endl;
    exit(1);
  }
}

void restore_cpu_state_from_v2(const SimpointCpuStateV2 &src, CPU_state &dst) {
  for (int i = 0; i < 32; ++i) {
    dst.gpr[i] = src.gpr[i];
  }
  for (int i = 0; i < 21; ++i) {
    dst.csr[i] = src.csr[i];
  }
  dst.pc = src.pc;
  dst.store_addr = src.store_addr;
  dst.store_data = src.store_data;
  dst.store_strb = src.store_strb;
  dst.store = src.store;
}

} // namespace

std::map<uint32_t, uint32_t> load_simpoints(const std::string &filename) {
  std::map<uint32_t, uint32_t> targets;
  std::ifstream in(filename);
  if (!in) {
    std::cerr << "Error: Could not open points file: " << filename << std::endl;
    exit(1);
  }
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#')
      continue;
    std::stringstream ss(line);
    uint32_t interval_id = 0;
    uint32_t sp_id = 0;
    if (ss >> interval_id >> sp_id) {
      targets[interval_id] = sp_id;
    }
  }
  return targets;
}

void Ref_cpu::bbv_init_file(const char *filename) {
  bbv_file.open(filename, std::ios::out | std::ios::trunc);
  if (!bbv_file.is_open()) {
    std::cerr << "Error: Could not open BBV output file!" << std::endl;
  }
  bbv_counts.resize(65536, 0);
}

void Ref_cpu::bbv_commit() {
  uint32_t bb_id = 0;
  auto it = global_pc_to_id.find(current_bb_head_pc);
  if (it != global_pc_to_id.end()) {
    bb_id = it->second;
  } else {
    bb_id = next_bb_id++;
    global_pc_to_id[current_bb_head_pc] = bb_id;
  }
  if (bb_id >= bbv_counts.size()) {
    size_t new_size = bbv_counts.size() * 2;
    if (new_size <= bb_id)
      new_size = bb_id + 1024;
    bbv_counts.resize(new_size, 0);
  }
  bbv_counts[bb_id] += current_bb_len;
}

void Ref_cpu::dump_bbv() {
  if (!bbv_file.is_open())
    return;
  bbv_file << "T";
  for (size_t id = 1; id < bbv_counts.size(); ++id) {
    if (bbv_counts[id] > 0) {
      bbv_file << " :" << id << ":" << bbv_counts[id];
      bbv_counts[id] = 0;
    }
  }
  bbv_file << "\n";
}

void Ref_cpu::save_checkpoint(const std::string &filename) {
  std::cerr << "save_checkpoint() is not implemented in this TraceSim build. "
               "Requested file: "
            << filename << std::endl;
  exit(1);
}

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
  if (memory == nullptr) {
    std::cerr << "Error: Memory not allocated." << std::endl;
    exit(1);
  }

  uint32_t first_word = 0;
  gz_read_pod(file, first_word);
  if (first_word != CKPT_MAGIC) {
    std::cerr << "Error: Invalid checkpoint magic. Only latest simpoint_sim "
                 "checkpoint format is supported."
              << std::endl;
    exit(1);
  }

  CkptHeader header = {};
  header.magic = first_word;
  gz_read_pod(file, header.version);
  gz_read_pod(file, header.ram_size);
  gz_read_pod(file, header.io_range_count);

  if (header.version != CKPT_VERSION) {
    std::cerr << "Error: Unsupported checkpoint version: " << header.version
              << std::endl;
    exit(1);
  }
  if (header.ram_size != ram_size) {
    std::cerr << "Error: Checkpoint RAM size mismatch. file=0x" << std::hex
              << header.ram_size << " sim=0x" << ram_size << std::dec
              << std::endl;
    exit(1);
  }

  uint64_t interval_inst_count = 0;
  SimpointCpuStateV2 ckpt_state = {};
  gz_read_pod(file, ckpt_state);
  restore_cpu_state_from_v2(ckpt_state, state);
  gz_read_pod(file, interval_inst_count);
  privilege = RISCV_MODE_U;
  sim_time = interval_inst_count;

  std::cout << "Restoring Memory..." << std::endl;
  gz_read_bytes(file, reinterpret_cast<uint8_t *>(memory), ram_size);

  const auto io_layout = expected_io_layout();
  if (header.io_range_count != io_layout.size()) {
    std::cerr << "Error: IO layout count mismatch. file=" << header.io_range_count
              << " sim=" << io_layout.size() << std::endl;
    exit(1);
  }

  io_words.clear();
  for (uint32_t i = 0; i < header.io_range_count; ++i) {
    CkptIoRange r = {};
    gz_read_pod(file, r);
    if (r.base != io_layout[i].base || r.size != io_layout[i].size) {
      std::cerr << "Error: IO layout mismatch at index " << i << ". file=[0x"
                << std::hex << r.base << ", 0x" << r.size << "] sim=[0x"
                << io_layout[i].base << ", 0x" << io_layout[i].size << "]"
                << std::dec << std::endl;
      exit(1);
    }
    std::vector<uint8_t> io_bytes(r.size, 0);
    gz_read_bytes(file, io_bytes.data(), io_bytes.size());
    for (uint32_t off = 0; off + 4 <= r.size; off += 4) {
      uint32_t word = static_cast<uint32_t>(io_bytes[off + 0]) |
                      (static_cast<uint32_t>(io_bytes[off + 1]) << 8) |
                      (static_cast<uint32_t>(io_bytes[off + 2]) << 16) |
                      (static_cast<uint32_t>(io_bytes[off + 3]) << 24);
      if (word != 0) {
        io_words[r.base + off] = word;
      }
    }
  }

  gzclose(file);
  std::cout << "Checkpoint restored from " << final_name << std::endl;
}
