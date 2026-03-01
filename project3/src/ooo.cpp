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

using namespace tinyrv;

void Core::issue() {
  // check input
  if (issue_queue_->empty())
    return;

  auto& is_data = issue_queue_->data();
  auto instr = is_data.instr;
  auto exe_flags = instr->getExeFlags();
  bool is_lsu = (instr->getFUType() == FUType::LSU);

  // check for structural hazards
  // TODO

  uint32_t rs1_data = 0, rs2_data = 0;
  uint32_t rs1_rob = -1, rs2_rob = -1;

  // load rs1 data
  // check the RAT if value is in the registe file
  // if not in the register file, check data is in the ROB
  // else set rs1_rob to the rob entry producing the data
  // remember to first check if the instruction actually uses rs1
  // HINT: should use RAT, ROB, and reg_file_
  // TODO:

  // load rs2 data
  // check the RAT if value is in the registe file
  // if not in the register file, check data is in the ROB
  // else set rs2_rob to the rob entry producing the data
  // remember to first check if the instruction actually uses rs1
  // HINT: should use RAT, ROB, and reg_file_
  // TODO:

  // allocate new ROB entry
  uint32_t rob_tag = ROB_.allocate(instr);

  // update the RAT if instruction is writing to the register file
  if (instr->getExeFlags().use_rd) {
    RAT_.set(instr->getRd(), rob_tag);
  }

#ifdef BP_ENABLE
  // Save checkpoint after RAT update so branches that write rd (JAL/JALR)
  // keep their mapping during recovery.
  if (instr->getBrOp() != BrOp::NONE) {
    if (!checkpoints_.save(rob_tag, RAT_)) {
      std::abort();
    }
  }
#endif

  if (is_lsu) {
    // allocate LSQ entry and set instruction metadata
    // TODO:
  }

  // issue instruction to reservation station
  // TODO:

  DT(2, "Issue: " << *instr);

  // pop issue queue
  issue_queue_->pop();
}

void Core::execute() {
  if (flush_pending_) {
    DT(2, "Flush: misprediction, rob=0x" << std::hex << flush_rob_ << ", pc=0x" << flush_pc_ << std::dec);
    this->pipeline_flush();
  }

  // find the next functional units that is done executing
  // and push its output result to the common data bus
  // The CDB can only serve one functional unit per cycle
  // HINT: should use CDB_ and FUs_
  for (auto fu : FUs_) {
    // TODO:
  }

  // schedule ready instructions to corresponding functional units
  // iterate through all reservation stations, check if the entry is valid, and operands are ready
  // once a candidate is found, issue the instruction to its corresponding nont-busy functional uni.
  // HINT: should use RS_ and FUs_
  for (uint32_t rs_index = 0; rs_index < RS_.size(); ++rs_index) {
    auto& entry = RS_.get_entry(rs_index);
    // TODO:
  }
}

void Core::writeback() {
  // check input
  if (CDB_.empty())
    return;

  // CDB broadcast
  auto& cdb_data = CDB_.result();

  // update reservation stations waiting for operands
  for (uint32_t rs_index = 0; rs_index < RS_.size(); ++rs_index) {
    auto& entry = RS_.get_entry(rs_index);
    if (entry.valid) {
      // TODO:
    }
  }

  // update ROB
  // TODO:

  // clear CDB
  // TODO:

  RS_.dump();
  LSQ_->dump();
}

void Core::commit() {
  // check input
  if (ROB_.empty())
    return;

  // commit ROB head entry
  uint32_t head_index = ROB_.head_index();
  uint32_t head_tag = ROB_.head_tag();
  auto& rob_head = ROB_.get_entry(head_index);
  if (rob_head.ready) {
    auto instr = rob_head.instr;
    auto exe_flags = instr->getExeFlags();

    // Notify LSQ to commit if instruction is load/store
    // TODO:

    // update register file if needed
    // TODO:

    // clear the RAT if still pointing to this ROB entry
    if (exe_flags.use_rd && RAT_.exists(instr->getRd())) {
      RAT_.clear_mapping(instr->getRd(), head_tag);
    }

    // clear checkpoint RAT mappings
    if (exe_flags.use_rd) {
      checkpoints_.clear_RAT_mapping(instr->getRd(), head_tag);
    }

    // pop ROB entry
    // TODO:

    DT(2, "Commit: " << *instr);

    assert(perf_stats_.instrs <= fetched_instrs_);
    ++perf_stats_.instrs;

    // handle program termination
    if (exe_flags.is_exit) {
      exited_ = true;
    }
  }

  ROB_.dump();
}

void Core::pipeline_flush() {
  // restore RAT from current flush checkpoint
  // TODO:

  // invalidate younger instructions in pipeline structures
  // include chejckpoints, ROB, reservation stations, LSQ, CDB, and functional units
  // TOOD:

  // reset pipeline queues and states
  // include decode/issue queue
  // TODO:
  exit_pending_ = false;
  fetch_lock_->write(false);

  // set PC to new path
  PC_ = flush_pc_;

  // terminate flush
  flush_pending_ = false;
}