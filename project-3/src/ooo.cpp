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

void Core::issue()
{
  // check input
  if (issue_queue_->empty())
    return;

  auto &is_data = issue_queue_->data();
  auto instr = is_data.instr;
  auto exe_flags = instr->getExeFlags();
  bool is_lsu = (instr->getFUType() == FUType::LSU);

  // check for structural hazards
  if (ROB_.full() || RS_.full())
    return;
  if (is_lsu && LSQ_->full())
    return;

  uint32_t rs1_data = 0, rs2_data = 0;
  uint32_t rs1_rob = -1, rs2_rob = -1;

  // load rs1 data
  if (exe_flags.use_rs1)
  {
    if (RAT_.exists(instr->getRs1()))
    {
      uint32_t rob_idx = RAT_.get(instr->getRs1());
      auto &rob_entry = ROB_.get_entry(rob_idx);
      if (rob_entry.ready)
      {
        rs1_data = rob_entry.result;
      }
      else
      {
        rs1_rob = rob_idx;
      }
    }
    else
    {
      rs1_data = reg_file_[instr->getRs1()];
    }
  }

  // load rs2 data
  if (exe_flags.use_rs2)
  {
    if (RAT_.exists(instr->getRs2()))
    {
      uint32_t rob_idx = RAT_.get(instr->getRs2());
      auto &rob_entry = ROB_.get_entry(rob_idx);
      if (rob_entry.ready)
      {
        rs2_data = rob_entry.result;
      }
      else
      {
        rs2_rob = rob_idx;
      }
    }
    else
    {
      rs2_data = reg_file_[instr->getRs2()];
    }
  }

  // allocate new ROB entry
  uint32_t rob_tag = ROB_.allocate(instr);

  // update the RAT if instruction is writing to the register file
  if (instr->getExeFlags().use_rd)
  {
    RAT_.set(instr->getRd(), rob_tag);
  }

#ifdef BP_ENABLE
  // Save checkpoint after RAT update so branches that write rd (JAL/JALR)
  // keep their mapping during recovery.
  if (instr->getBrOp() != BrOp::NONE)
  {
    if (!checkpoints_.save(rob_tag, RAT_))
    {
      std::abort();
    }
  }
#endif

  if (is_lsu)
  {
    // allocate LSQ entry and set instruction metadata
    uint32_t lsq_idx = LSQ_->allocate(rob_tag, instr);
    instr_meta_t meta;
    meta.lsu.lsq_idx = lsq_idx;
    instr->setMetaData(meta);
  }

  // issue instruction to reservation station
  RS_.issue(rob_tag, rs1_rob, rs2_rob, rs1_data, rs2_data, instr);

  DT(2, "Issue: " << *instr);

  // pop issue queue
  issue_queue_->pop();
}

void Core::execute()
{
  if (flush_pending_)
  {
    DT(2, "Flush: misprediction, rob=0x" << std::hex << flush_rob_ << ", pc=0x" << flush_pc_ << std::dec);
    this->pipeline_flush();
  }

  // find the next functional units that is done executing
  // and push its output result to the common data bus
  // The CDB can only serve one functional unit per cycle
  // HINT: should use CDB_ and FUs_
  for (auto fu : FUs_)
  {
    if (!fu->empty() && CDB_.empty())
    {
      CDB_.push(fu->pop());
    }
  }

  // schedule ready instructions to corresponding functional units
  // iterate through all reservation stations, check if the entry is valid, and operands are ready
  // once a candidate is found, issue the instruction to its corresponding nont-busy functional uni.
  // HINT: should use RS_ and FUs_
  for (uint32_t rs_index = 0; rs_index < RS_.size(); ++rs_index)
  {
    auto &entry = RS_.get_entry(rs_index);
    if (!entry.valid || !entry.operands_ready())
      continue;
    auto fu_type = entry.instr->getFUType();
    auto fu = FUs_.at((int)fu_type);
    if (fu->full())
      continue;
    fu->push(entry.instr, entry.rd_rob, entry.rs1_data, entry.rs2_data);
    RS_.release(rs_index);
  }
}

void Core::writeback()
{
  // check input
  if (CDB_.empty())
    return;

  // CDB broadcast
  auto &cdb_data = CDB_.result();

  // update reservation stations waiting for operands
  for (uint32_t rs_index = 0; rs_index < RS_.size(); ++rs_index)
  {
    auto &entry = RS_.get_entry(rs_index);
    if (entry.valid)
    {
      entry.update_operands(cdb_data);
    }
  }

  // update ROB
  ROB_.update(cdb_data);

  // clear CDB
  CDB_.pop();

  RS_.dump();
  LSQ_->dump();
}

void Core::commit()
{
  // check input
  if (ROB_.empty())
    return;

  // commit ROB head entry
  uint32_t head_index = ROB_.head_index();
  uint32_t head_tag = ROB_.head_tag();
  auto &rob_head = ROB_.get_entry(head_index);
  if (rob_head.ready)
  {
    auto instr = rob_head.instr;
    auto exe_flags = instr->getExeFlags();

    // Notify LSQ to commit if instruction is load/store
    if (exe_flags.is_load || exe_flags.is_store)
    {
      LSQ_->commit(head_tag);
    }

    // update register file if needed
    if (exe_flags.use_rd)
    {
      reg_file_[instr->getRd()] = rob_head.result;
    }

    // clear the RAT if still pointing to this ROB entry
    if (exe_flags.use_rd && RAT_.exists(instr->getRd()))
    {
      RAT_.clear_mapping(instr->getRd(), head_tag);
    }

    // clear checkpoint RAT mappings
    if (exe_flags.use_rd)
    {
      checkpoints_.clear_RAT_mapping(instr->getRd(), head_tag);
    }

    // pop ROB entry
    ROB_.pop();

    DT(2, "Commit: " << *instr);

    assert(perf_stats_.instrs <= fetched_instrs_);
    ++perf_stats_.instrs;

    // handle program termination
    if (exe_flags.is_exit)
    {
      exited_ = true;
    }
  }

  ROB_.dump();
}

void Core::pipeline_flush()
{
  // restore RAT from current flush checkpoint
  checkpoints_.restore(flush_rob_, &RAT_);

  // invalidate younger instructions in pipeline structures
  checkpoints_.invalidate(flush_rob_);
  ROB_.invalidate(flush_rob_);
  RS_.invalidate(flush_rob_);
  LSQ_->invalidate(flush_rob_);
  CDB_.invalidate(flush_rob_);
  for (auto fu : FUs_)
  {
    fu->invalidate(flush_rob_);
  }

  // reset pipeline queues and states
  decode_queue_->reset();
  issue_queue_->reset();
  exit_pending_ = false;
  fetch_lock_->write(false);

  // set PC to new path
  PC_ = flush_pc_;

  // terminate flush
  flush_pending_ = false;
}