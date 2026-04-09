// Copyright 2025 Blaise Tine
//
// Licensed under the Apache License;
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <string>
#include <vector>
#include <list>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <memory>
#include <set>
#include <iostream>
#include <simobject.h>
#include "debug.h"
#include "types.h"
#include "config.h"
#include "cache.h"
#include "val_reg.h"
#include "fifo_reg.h"
#include "instr.h"
#include "BP.h"
#include "RAT.h"
#include "RS.h"
#include "LSQ.h"
#include "ROB.h"
#include "FU.h"
#include "CDB.h"
#include "checkpoints.h"

namespace tinyrv {

class ProcessorImpl;
class Instr;

class Core : public SimObject<Core> {
public:
  struct PerfStats {
    uint64_t cycles;
    uint64_t instrs;

    PerfStats()
      : cycles(0)
      , instrs(0)
    {}
  };

  Core(const SimContext& ctx, uint32_t core_id, ProcessorImpl* processor);
  ~Core();

  void reset();

  void tick();

  bool running() const;

  bool check_exit(Word* exitcode, bool riscv_test) const;

  void showStats();

  Cache::Ptr get_icache() const {
    return icache_;
  }

  Cache::Ptr get_dcache() const {
    return dcache_;
  }

private:

  Instr::Ptr decode(uint32_t instr_code, uint32_t PC, uint64_t uuid) const;

  void set_csr(uint32_t addr, uint32_t value);

  uint32_t get_csr(uint32_t addr);

  void update_branch(uint32_t br_target, bool br_taken, Instr::Ptr instr, uint32_t rob_tag);

  void pipeline_flush();

  void writeToStdOut(const void* data);

  void cout_flush();

  struct id_data_t {
    uint32_t instr_code;
    Word     PC;
    Word     next_PC;
    bool     pred;
    uint64_t uuid;
  };

  struct is_data_t {
    Instr::Ptr instr;
  };

  struct ex_data_t {
    Instr::Ptr instr;
    uint32_t rs1_data;
    uint32_t rs2_data;
  };

  void fetch();
  void decode();
  void issue();
  void execute();
  void writeback();
  void commit();

  uint32_t core_id_;
  ProcessorImpl* processor_;
  Cache::Ptr icache_;
  Cache::Ptr dcache_;

  std::vector<Word> reg_file_;
  Word PC_;

  FiFoReg<id_data_t>::Ptr decode_queue_;
  FiFoReg<is_data_t>::Ptr issue_queue_;

  ReorderBuffer       ROB_;
  RegisterAliasTable  RAT_;
  ReservationStation  RS_;
  LoadStoreQueue::Ptr LSQ_;
  LSU::Ptr            LSU_;
  ALU::Ptr            ALU_;
  BRU::Ptr            BRU_;
  SFU::Ptr            SFU_;
  CommonDataBus       CDB_;
  std::vector<FunctionalUnit::Ptr> FUs_;

  ValReg<bool>::Ptr fetch_lock_;
  bool exit_pending_;
  bool exited_;

  std::stringstream cout_buf_;

  uint64_t uuid_ctr_;

  PerfStats perf_stats_;
  uint64_t fetched_instrs_;

  Checkpoints checkpoints_;

  bool flush_pending_;
  uint32_t flush_rob_;
  uint32_t flush_pc_;

  bool fetch_req_sent_;
  uint32_t fetch_pending_pc_;
  uint64_t fetch_pending_uuid_;

#ifdef BP_ENABLE
  BP BP_;
#endif

  friend class ALU;
  friend class BRU;
  friend class LSU;
  friend class SFU;
  friend class LoadStoreQueue;
};

} // namespace tinyrv
