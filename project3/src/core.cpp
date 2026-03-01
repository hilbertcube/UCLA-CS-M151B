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

#include <iostream>
#include <iomanip>
#include <string.h>
#include <assert.h>
#include <util.h>
#include "types.h"
#include "core.h"
#include "debug.h"
#include "processor_impl.h"
#include "BP.h"

using namespace tinyrv;

Core::Core(const SimContext& ctx, uint32_t core_id, ProcessorImpl* processor)
    : SimObject(ctx, "core")
    , core_id_(core_id)
    , processor_(processor)
    , icache_(Cache::Create("icache", ICACHE_SIZE, ICACHE_NUM_WAYS))
    , dcache_(Cache::Create("dcache", DCACHE_SIZE, DCACHE_NUM_WAYS))
    , reg_file_(NUM_REGS)
    , decode_queue_(FiFoReg<id_data_t>::Create("idq"))
    , issue_queue_(FiFoReg<is_data_t>::Create("isq"))
    , ROB_(ROB_SIZE)
    , RAT_(NUM_REGS)
    , RS_(NUM_RSS)
    , LSQ_(LoadStoreQueue::Create("lsq", NUM_LSQS, this))
    , FUs_(NUM_FUS)
    , fetch_lock_(ValReg<bool>::Create("fetch_lock", false))
{
  // create functional units
  ALU_ = SimPlatform::instance().create_object<ALU>("alu", this);
  LSU_ = SimPlatform::instance().create_object<LSU>("lsu", this);
  BRU_ = SimPlatform::instance().create_object<BRU>("bru", this);
  SFU_ = SimPlatform::instance().create_object<SFU>("sfu", this);
  FUs_.at((int)FUType::ALU) = ALU_;
  FUs_.at((int)FUType::LSU) = LSU_;
  FUs_.at((int)FUType::BRU) = BRU_;
  FUs_.at((int)FUType::SFU) = SFU_;

  // initialize register file at x0
  reg_file_.at(0) = 0;

  this->reset();
}

Core::~Core() {}

void Core::reset() {
  decode_queue_->reset();
  issue_queue_->reset();
  LSQ_->reset();
  for (auto& fu : FUs_) {
    fu->reset();
  }

  PC_ = STARTUP_ADDR;

  uuid_ctr_ = 0;

  fetched_instrs_ = 0;
  perf_stats_ = PerfStats();

  fetch_lock_->reset();
  exited_ = false;
  fetch_req_sent_ = false;
  fetch_pending_pc_ = 0;
  fetch_pending_uuid_ = 0;
  checkpoints_.reset();
  flush_pending_ = false;
  exit_pending_ = false;
#ifdef BP_ENABLE
  BP_.reset();
#endif
}

void Core::tick() {
  this->commit();
  this->writeback();
  this->execute();
  this->issue();
  this->decode();
  this->fetch();

  ++perf_stats_.cycles;
  DPN(2, std::flush);
}

void Core::fetch() {
  // check input and back+-pressure
  if (fetch_lock_->read() || decode_queue_->full())
    return;

  // don't fetch past program termination once exit is in-flight
  if (exit_pending_)
    return;

  // allocate a new uuid
  if (!fetch_req_sent_) {
    fetch_pending_uuid_ = uuid_ctr_++;
    fetch_pending_pc_ = PC_;
  }

  if (!fetch_req_sent_) {
    cache_req_t req{false, fetch_pending_pc_, 0, 0, static_cast<uint32_t>(fetch_pending_uuid_)};
    if (!icache_->core_req.try_send(req, 0)) {
      return;
    }
    fetch_req_sent_ = true;
  }

  if (icache_->core_rsp.empty()) {
    return;
  }

  auto rsp = icache_->core_rsp.peek();
  icache_->core_rsp.pop();
  uint32_t instr_code = rsp.data;
  uint64_t uuid = fetch_pending_uuid_;
  uint32_t current_PC = fetch_pending_pc_;
  fetch_req_sent_ = false;

  // Branch flush can change PC while an I-cache request is in-flight.
  // In that case, drop this stale response and re-request at new PC.
  if (current_PC != PC_) {
    return;
  }

  DT(2, "Fetch: instr=0x" << instr_code << ", PC=0x" << std::hex << current_PC << std::dec << " (#" << uuid << ")");

  // advance program counter
  PC_ = current_PC + 4;

  ++fetched_instrs_;

#ifdef BP_ENABLE
  // use branch predictor
  bool pred = BP_.predict(PC_);
  if (pred) {
    PC_ = BP_.get_nextPC();
  }
#else
  // we stall the fetch stage until branch resolution
  bool pred = false;
  fetch_lock_->write(true);
#endif

  // move instruction data to next stage
  decode_queue_->push({instr_code, current_PC, PC_, pred, uuid});
}

void Core::decode() {
  // check input and back-pressure
  if (decode_queue_->empty() || issue_queue_->full())
    return;

  auto& id_data = decode_queue_->data();

  // instruction decode
  auto instr = this->decode(id_data.instr_code, id_data.PC, id_data.uuid);

  // pass branch predictor metadata to instruction
  instr_meta_t meta;
  meta.bru = {id_data.next_PC, id_data.pred};
  instr->setMetaData(meta);

  DT(2, "Decode: " << *instr);

  // stop front-end speculation once program termination is decoded
  if (instr->getExeFlags().is_exit) {
    exit_pending_ = true;
    fetch_lock_->write(true);
  }

#ifndef BP_ENABLE
  // release fetch stage if not a branch
  // keep fetch stage locked if exiting program
  if (instr->getBrOp() == BrOp::NONE
   && !instr->getExeFlags().is_exit) {
    fetch_lock_->write(false); // unlock fetch stage
  }
#endif

  // move instruction data to next stage
  issue_queue_->push({instr});
  decode_queue_->pop();
}

void Core::update_branch(uint32_t br_target, bool br_taken, Instr::Ptr instr, uint32_t rob_tag) {
#ifdef BP_ENABLE
  auto meta = instr->getMetaData();
  BP_.update(instr->getPC(), meta.bru.taken, meta.bru.target, br_taken, br_target);
  bool mispred = (meta.bru.target != br_target);
  assert(rob_tag != INVALID_ROB);
  if (mispred) {
    bool is_older = true;
    if (flush_pending_) {
      uint32_t tag_mask = (ROB_SIZE * 2) - 1;
      uint32_t diff = (flush_rob_ - rob_tag) & tag_mask;
      is_older = (diff > 0 && diff < ROB_SIZE);
    }
    if (!flush_pending_ || is_older) {
      flush_pending_ = true;
      flush_rob_ = rob_tag;
      flush_pc_ = br_target;
      // stall fetch stage until flush complete
      fetch_lock_->write(true);
    }
  } else {
    // release this branch checkpoint
    checkpoints_.release(rob_tag);
  }
#else
  __unused(br_taken, instr, rob_tag);
  PC_ = br_target;
  fetch_lock_->write(false); // release fetch stage
#endif
  DT(2, "Branch: " << (br_taken ? "taken" : "not-taken") << ", target=0x" << std::hex << br_target << std::dec << " (#" << instr->getId() << ")");
}

uint32_t Core::get_csr(uint32_t addr) {
  // stall-independent mcycle workaround for software timing consistency
  uint64_t ideal_mcycles = (perf_stats_.instrs-1) + 5;
  switch (addr) {
  case VX_CSR_MHARTID:
  case VX_CSR_SATP:
  case VX_CSR_PMPCFG0:
  case VX_CSR_PMPADDR0:
  case VX_CSR_MSTATUS:
  case VX_CSR_MISA:
  case VX_CSR_MEDELEG:
  case VX_CSR_MIDELEG:
  case VX_CSR_MIE:
  case VX_CSR_MTVEC:
  case VX_CSR_MEPC:
  case VX_CSR_MNSTATUS:
    return 0;
  case VX_CSR_MCYCLE: // NumCycles
    return ideal_mcycles & 0xffffffff;
  case VX_CSR_MCYCLE_H: // NumCycles
    return (uint32_t)(ideal_mcycles >> 32);
  case VX_CSR_MINSTRET: // NumInsts
    return perf_stats_.instrs & 0xffffffff;
  case VX_CSR_MINSTRET_H: // NumInsts
    return (uint32_t)(perf_stats_.instrs >> 32);
  default:
    std::cout << std::hex << "Error: invalid CSR read addr=0x" << addr << std::endl;
    std::abort();
    return 0;
  }
}

void Core::set_csr(uint32_t addr, uint32_t value) {
  switch (addr) {
  case VX_CSR_SATP:
  case VX_CSR_MSTATUS:
  case VX_CSR_MEDELEG:
  case VX_CSR_MIDELEG:
  case VX_CSR_MIE:
  case VX_CSR_MTVEC:
  case VX_CSR_MEPC:
  case VX_CSR_PMPCFG0:
  case VX_CSR_PMPADDR0:
  case VX_CSR_MNSTATUS:
    break;
  default: {
      std::cout << std::hex << "Error: invalid CSR write addr=0x" << addr << ", value=0x" << value << std::endl;
      std::abort();
    }
  }
}

void Core::writeToStdOut(const void* data) {
  char c = *(char*)data;
  cout_buf_ << c;
  if (c == '\n') {
    std::cout << cout_buf_.str() << std::flush;
    cout_buf_.str("");
  }
}

void Core::cout_flush() {
  auto str = cout_buf_.str();
  if (!str.empty()) {
    std::cout << str << std::endl;
  }
}

bool Core::check_exit(Word* exitcode, bool riscv_test) const {
  if (exited_) {
    Word ec = reg_file_.at(3);
    if (riscv_test) {
      *exitcode = (1 - ec);
    } else {
      *exitcode = ec;
    }
    return true;
  }
  return false;
}

bool Core::running() const {
  return (perf_stats_.instrs != fetched_instrs_) || (fetched_instrs_ == 0);
}

void Core::showStats() {
  auto pct = [](uint64_t hits, uint64_t total) -> double {
    if (total == 0)
      return 0.0;
    return (100.0 * static_cast<double>(hits)) / static_cast<double>(total);
  };

  std::cout << std::dec << "PERF: instrs=" << perf_stats_.instrs << ", cycles=" << perf_stats_.cycles << std::endl;
  auto& lsq = LSQ_->get_stats();
  std::cout << std::fixed << std::setprecision(2)
            << "LSQ: loads=" << lsq.load_issued
            << ", stores=" << lsq.store_issued
            << ", fwd_hits=" << pct(lsq.load_fwd_hits, lsq.load_issued) << "%"
            << ", alias_stalls=" << pct(lsq.aliasing_stalls, lsq.load_issued) << "%"
            << std::endl;

  auto& ic = icache_->get_stats();
  std::cout << std::fixed << std::setprecision(2)
            << "ICACHE: reads=" << ic.core_reads
            << " (hit%=" << pct(ic.read_hits, ic.core_reads) << ")"
            << std::endl;

  auto& dc = dcache_->get_stats();
  uint64_t dc_total = dc.core_reads + dc.core_writes;
  uint64_t dc_hits = dc.read_hits + dc.write_hits;
  std::cout << "DCACHE: reads=" << dc_total
            << " (hit%=" << pct(dc_hits, dc_total) << ")"
            << ", writes=" << dc.core_reads
            << " (hit%=" << pct(dc.read_hits, dc.core_reads) << ")"
            << std::endl;
#ifdef BP_ENABLE
  auto& bp = BP_.get_stats();
  std::cout << std::fixed << std::setprecision(2)
            << "BP: MPKI=" << bp.mpki(perf_stats_.instrs)
            << ", accuracy (direction=" << (100.0 * bp.direction_accuracy())
            << "%, target=" << (100.0 * bp.target_accuracy()) << "%)"
            << std::endl;
#endif
}
