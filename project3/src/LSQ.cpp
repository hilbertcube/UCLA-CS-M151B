// Copyright 2026 Blaise Tine
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
#include <assert.h>
#include <optional>
#include "util.h"
#include "debug.h"
#include "LSQ.h"
#include "core.h"

using namespace tinyrv;

namespace {

// ----------------------------------------------------------------------------
// Small helpers used by LSQ scheduling/forwarding logic.
// ----------------------------------------------------------------------------

inline uint32_t mem_data_bytes(MemDataType func3) {
  return 1u << (static_cast<uint32_t>(func3) & 0x3u);
}

inline bool ranges_overlap(uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1) {
  return (a0 < b1) && (b0 < a1);
}

inline bool is_ready_store(const LoadStoreQueue::lsq_entry_t& entry) {
  // A store is ready to execute when it is committed and both its effective address and data are available.
  // TODO:
}

inline bool is_ready_load(const LoadStoreQueue::lsq_entry_t& entry) {
  // A load is ready to execute when it is pending and its effective address is available.
  // TODO:
}

inline std::optional<uint32_t> try_forward_from_store(
  const LoadStoreQueue::lsq_entry_t& load,
  const LoadStoreQueue::lsq_entry_t& store) {
  // Try to forward data from `store` to `load`.
  // Returns forwarded value when load bytes are fully covered by store bytes;
  // otherwise returns std::nullopt.
  auto load_func3 = static_cast<MemDataType>(load.instr->getFunc3());
  auto store_func3 = static_cast<MemDataType>(store.instr->getFunc3());

  uint32_t load_bytes = mem_data_bytes(load_func3);
  uint32_t store_bytes = mem_data_bytes(store_func3);

  uint32_t load_start = load.addr;
  uint32_t load_end = load_start + load_bytes;
  uint32_t store_start = store.addr;
  uint32_t store_end = store_start + store_bytes;

  if (load_start < store_start || load_end > store_end)
    return std::nullopt;

  uint32_t delta = load_start - store_start;
  uint32_t raw = store.data >> (delta * 8);

  switch (load_func3) {
  case MemDataType::LB:
    return sext(raw & 0xff, 8);
  case MemDataType::LH:
    return sext(raw & 0xffff, 16);
  case MemDataType::LW:
    return raw;
  case MemDataType::LBU:
    return raw & 0xff;
  case MemDataType::LHU:
    return raw & 0xffff;
  default:
    return std::nullopt;
  }
}

} // namespace

LoadStoreQueue::LoadStoreQueue(const SimContext& ctx, const char* name, uint32_t size, Core* core)
  : SimObject<LoadStoreQueue>(ctx, name)
  , store_(size)
  , head_index_(0)
  , tail_index_(0)
  , count_(0)
  , core_(core)
  , perf_stats_()
{
  this->reset();
}

LoadStoreQueue::~LoadStoreQueue() {
  // --
}

void LoadStoreQueue::reset() {
  head_index_ = 0;
  tail_index_ = 0;
  count_ = 0;
  perf_stats_ = PerfStats();
  for (uint32_t i = 0; i < store_.size(); ++i) {
    store_[i].clear();
  }
}

uint32_t LoadStoreQueue::allocate(uint32_t rob_tag, Instr::Ptr instr) {
  // Reserve a free LSQ slot for a newly issued memory instruction.
  // The entry starts in Pending state and will be filled later with
  // address/data as the instruction executes.
  assert(!this->full());
  for (uint32_t n = 0; n < store_.size(); ++n) {
    uint32_t idx = (tail_index_ + n) % store_.size();
    if (!store_[idx].valid) {
      store_[idx] = {true, lsq_state_t::Pending, false, false, rob_tag, 0, 0, instr};
      tail_index_ = (idx + 1) % store_.size();
      ++count_;
      return idx;
    }
  }
  std::abort();
}

void LoadStoreQueue::tick() {
  // Find the oldest actionable load/store and try to issue it to LSU.
  if (count_ == 0)
    return;

  // pick oldest actionable entry,
  int32_t oldest_idx = select_oldest_actionable_idx();
  if (oldest_idx < 0)
    return;

  auto& entry = store_[oldest_idx];
  assert(entry.valid);
  auto exe_flags = entry.instr->getExeFlags();

  // Try to issue the oldest actionable store.
  if (exe_flags.is_store) {
    assert(entry.addr_valid && entry.data_valid && entry.state == lsq_state_t::Committed);
    // Stores are only issued when committed, so we don't need to analyze dependencies or forwarding.
    if (core_->LSU_->issue_store(entry.addr, entry.data, oldest_idx)) {
      ++perf_stats_.store_issued;
      this->release_entry(oldest_idx);
    }
    return;
  }

  // Try to issue the oldest actionable load.
  if (exe_flags.is_load) {
    assert(entry.addr_valid && entry.state == lsq_state_t::Pending);

    // Analyze load dependencies and forwarding opportunities.
    bool can_forward = false;
    uint32_t fwd_data = 0;
    if (!analyze_load_dependencies(oldest_idx, &can_forward, &fwd_data)) {
      ++perf_stats_.aliasing_stalls;
      return;
    }

    // Issue the load with forwarding info.
    if (core_->LSU_->issue_load(entry.addr, fwd_data, oldest_idx, can_forward)) {
      if (can_forward)
        ++perf_stats_.load_fwd_hits;

      ++perf_stats_.load_issued;

      if (!can_forward)
        entry.state = lsq_state_t::Issued;
    }
  }
}

int32_t LoadStoreQueue::select_oldest_actionable_idx() {
  int32_t oldest_idx = -1;
  // Pick the oldest LSQ entry that is ready to issue this cycle.
  for (uint32_t i = 0; i < store_.size(); ++i) {
    auto& entry = store_[i];
    bool actionable = is_ready_store(entry) || is_ready_load(entry);
    if (!actionable)
      continue;
    if (oldest_idx < 0 || is_rob_younger(store_[oldest_idx].rob_tag, entry.rob_tag)) {
      oldest_idx = i;
    }
  }

  return oldest_idx;
}

bool LoadStoreQueue::analyze_load_dependencies(uint32_t load_idx, bool* can_forward, uint32_t* fwd_data) {
  // Analyze one pending load and decide:
  // - return value: true  => can issue this cycle
  //                 false => blocked (must wait)
  // - output args : forwarding decision/data
  auto& load = store_[load_idx];
  assert(load.valid && load.instr && load.addr_valid && load.state == LoadStoreQueue::lsq_state_t::Pending);
  assert(can_forward && fwd_data);

  *can_forward = false;
  *fwd_data = 0;
  uint32_t fwd_rob_tag = INVALID_ROB;
  auto load_func3 = static_cast<MemDataType>(load.instr->getFunc3());
  uint32_t load_bytes = mem_data_bytes(load_func3);
  uint32_t load_start = load.addr;
  uint32_t load_end   = load_start + load_bytes;

  for (uint32_t i = 0; i < store_.size(); ++i) {
    auto& older = store_[i];

    // skip invalid entries
    if (!older.valid)
      continue;

    // skip self
    if (i == load_idx)
      continue;

    // skip younger entries
    if (!is_rob_younger(load.rob_tag, older.rob_tag))
      continue;

    auto older_flags = older.instr->getExeFlags();

    // Step 1) Check older loads first.
    // If an older load already produced the same value at the same address and their func3 match,
    // we can reuse it directly (load-to-load forwarding).
    if (older_flags.is_load) {
      bool is_candidate = /* TODO */;
      if (is_candidate) {
        if (!*can_forward || is_rob_younger(older.rob_tag, fwd_rob_tag)) {
          *can_forward = true;
          *fwd_data = older.data;
          fwd_rob_tag = older.rob_tag;
        }
      }
    } else {
      assert(older_flags.is_store);

      // Step 2) For older stores, if address is not yet available, we must stall.
      // We cannot analyze dependencies without the store address.
      if (!older.addr_valid) {
        return false;
      }

      // Step 3) Unknown older store address => conservative stall.
      // Without the store address, we cannot rule out a dependency.
      if (!older.addr_valid) {
        return false;
      }

      // Step 4) If byte ranges do not overlap, this store is not a dependency.
      auto store_func3 = static_cast<MemDataType>(older.instr->getFunc3());
      uint32_t store_bytes = mem_data_bytes(store_func3);
      uint32_t store_start = older.addr;
      uint32_t store_end   = store_start + store_bytes;
      if (!ranges_overlap(load_start, load_end, store_start, store_end))
        continue; // keep analyzing younger stores.

      // Step 5) For overlapping stores, try store-to-load forwarding.
      // If successful, keep the youngest valid forwarding source.
      if (older.data_valid) {
        auto candidate = try_forward_from_store(load, older);
        if (candidate.has_value()) {
          if (!*can_forward || is_rob_younger(older.rob_tag, fwd_rob_tag)) {
            *can_forward = true;
            *fwd_data = *candidate;
            fwd_rob_tag = older.rob_tag;
          }
          // Even if we can forward from this store, there may be a younger store that can also forward.
          continue; // keep analyzing younger stores.
        }
      }

      // Step 6) Overlap exists and forwarding is not possible => stall.
      return false;
    }
  }

  return true;
}

void LoadStoreQueue::update_address(uint32_t lsq_idx, uint32_t addr) {
  // Set the effective address for the LSQ entry at the given index as valid.
  assert(lsq_idx < store_.size());
  auto& entry = store_[lsq_idx];
  assert(entry.valid);
  entry.addr = addr;
  entry.addr_valid = true;
}

bool LoadStoreQueue::update_data(uint32_t lsq_idx, uint32_t data) {
  // Set the data for the LSQ entry at the given index as valid,
  // and update the entry state to Writeback if not already Voided.
  // Voided are entries that were in-flight during a mis-speculation
  // and should be ignored until they are committed and retired from the LSQ.
  assert(lsq_idx < store_.size());
  auto& entry = store_[lsq_idx];
  assert(entry.valid);
  if (entry.state != lsq_state_t::Voided) {
    entry.data = data;
    entry.data_valid = true;
    entry.state = lsq_state_t::Writeback;
    return true;
  } else {
    this->release_entry(lsq_idx);
    return false;
  }
}

bool LoadStoreQueue::find_by_rob(uint32_t rob_tag, uint32_t* index) const {
  // Find the LSQ entry corresponding to the given ROB tag.
  for (uint32_t idx = 0; idx < store_.size(); ++idx) {
    if (store_[idx].valid && store_[idx].rob_tag == rob_tag) {
      if (index) {
        *index = idx;
      }
      return true;
    }
  }
  return false;
}

void LoadStoreQueue::commit(uint32_t rob_tag) {
  // Commit the LSQ entry corresponding to the given ROB tag.
  uint32_t index = 0;
  if (!this->find_by_rob(rob_tag, &index))
    return;
  auto& entry = store_[index];
  assert(entry.valid);

  auto exe_flags = entry.instr->getExeFlags();
  if (exe_flags.is_store) {
    entry.state = lsq_state_t::Committed;
  } else {
    this->release_entry(index);
  }
}

void LoadStoreQueue::invalidate(uint32_t rob_tag) {
  // Invalidate the LSQ entry corresponding to the given ROB tag, and any younger entries.
  uint32_t new_count = 0;
  for (uint32_t idx = 0; idx < store_.size(); ++idx) {
    auto& entry = store_[idx];
    if (!entry.valid)
      continue;

    bool is_younger = is_rob_younger(entry.rob_tag, rob_tag);
    if (is_younger) {
      if (entry.state == lsq_state_t::Issued) {
        // Younger in-flight entry: keep slot until late response arrives.
        entry.state = lsq_state_t::Voided;
        new_count++;
      } else {
        entry.clear();
      }
    } else {
      // Older entry survives flush unchanged.
      new_count++;
    }
  }

  // Recount valid entries and move head to the oldest available slot.
  count_ = new_count;
  if (count_ == 0) {
    head_index_ = tail_index_;
  } else if (!store_[head_index_].valid) {
    for (uint32_t i = 0; i < store_.size(); ++i) {
      uint32_t idx = (head_index_ + i) % store_.size();
      if (store_[idx].valid) {
        head_index_ = idx;
        break;
      }
    }
  }
}

void LoadStoreQueue::release_entry(uint32_t lsq_idx) {
  // Release the LSQ entry at the given index.
  // Update the head and tail pointers as needed.
  assert(lsq_idx < store_.size() && store_[lsq_idx].valid);

  store_[lsq_idx].clear();
  assert(count_ > 0);
  --count_;

  if (count_ == 0) {
    head_index_ = tail_index_;
    return;
  }

  if (lsq_idx == head_index_) {
    for (uint32_t i = 0; i < store_.size(); ++i) {
      uint32_t idx = (head_index_ + i) % store_.size();
      if (store_[idx].valid) {
        head_index_ = idx;
        return;
      }
    }
  }
}

void LoadStoreQueue::dump() const {
  for (uint32_t idx = 0; idx < store_.size(); ++idx) {
    auto& entry = store_[idx];
    if (entry.valid) {
      DT(4, "LSQ[" << idx << "] rob=" << entry.rob_tag
          << ", state=" << static_cast<uint32_t>(entry.state)
          << ", addr_valid=" << entry.addr_valid
          << ", data_valid=" << entry.data_valid
          << " (#" << entry.instr->getId() << ")");
    }
  }
}