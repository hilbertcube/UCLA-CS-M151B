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

#pragma once

#include <vector>
#include <deque>
#include <simobject.h>
#include "cache.h"
#include "instr.h"
#include "CDB.h"
#include "types.h"

namespace tinyrv {

class Core;

class LoadStoreQueue : public SimObject<LoadStoreQueue> {
public:

  struct PerfStats {
    uint64_t load_fwd_hits;
    uint64_t load_issued;
    uint64_t store_issued;
    uint64_t aliasing_stalls;

    PerfStats()
      : load_fwd_hits(0)
      , load_issued(0)
      , store_issued(0)
      , aliasing_stalls(0)
    {}
  };

  enum class lsq_state_t {
    Pending,  // entry allocated but not yet issued to LSU
    Issued,   // issued to LSU but not yet completed
    Writeback,// completed by LSU but not yet committed
    Committed,// committed to architectural state
    Voided    // invalidated but already issued
  };

  struct lsq_entry_t {
    bool valid;        // entry allocated
    lsq_state_t state; // entry state
    bool addr_valid;   // effective address computed by AGU
    bool data_valid;   // store value or loaded result available

    uint32_t rob_tag;  // owning ROB entry
    uint32_t addr;     // effective address
    uint32_t data;     // store value or load result
    Instr::Ptr instr;  // instruction

    void clear() {
      valid = false;
      state = lsq_state_t::Pending;
      addr_valid = false;
      data_valid = false;
      rob_tag = INVALID_ROB;
      addr = 0;
      data = 0;
      instr = nullptr;
    }
  };

  LoadStoreQueue(const SimContext& ctx, const char* name, uint32_t size, Core* core);

  ~LoadStoreQueue();

  void reset();

  uint32_t allocate(uint32_t rob_tag, Instr::Ptr instr);

  void update_address(uint32_t lsq_idx, uint32_t addr);

  bool update_data(uint32_t lsq_idx, uint32_t data);

  const lsq_entry_t& get_entry(uint32_t lsq_idx) const {
    assert(lsq_idx < store_.size());
    return store_[lsq_idx];
  }

  void invalidate(uint32_t rob_tag);

  void tick();

  void commit(uint32_t rob_tag);

  bool full() const {
    return (count_ == store_.size());
  }

  bool empty() const {
    return (count_ == 0);
  }

  void dump() const;

  const PerfStats& get_stats() const {
    return perf_stats_;
  }

private:
  bool find_by_rob(uint32_t rob_tag, uint32_t* index) const;

  void release_entry(uint32_t lsq_idx);

  int32_t select_oldest_actionable_idx();

  bool analyze_load_dependencies(uint32_t load_idx, bool* can_forward, uint32_t* fwd_data);

  std::vector<lsq_entry_t> store_;
  uint32_t head_index_;
  uint32_t tail_index_;
  uint32_t count_;
  Core*    core_;
  PerfStats perf_stats_;
};

} // namespace tinyrv