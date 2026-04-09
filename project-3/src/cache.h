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

#include <array>
#include <memory>
#include <vector>
#include <simobject.h>
#include "memory.h"
#include "cache_repl.h"

namespace tinyrv {

struct cache_req_t {
  bool     is_write;
  uint64_t addr;
  uint64_t byteen;
  uint32_t data;
  uint32_t user;
};

struct cache_rsp_t {
  uint32_t data;
  uint32_t user;
};

class Cache : public SimObject<Cache> {
public:
  struct PerfStats {
    uint64_t core_reads;
    uint64_t core_writes;
    uint64_t read_hits;
    uint64_t read_misses;
    uint64_t write_hits;
    uint64_t write_misses;

    PerfStats()
      : core_reads(0)
      , core_writes(0)
      , read_hits(0)
      , read_misses(0)
      , write_hits(0)
      , write_misses(0)
    {}
  };

  SimChannel<cache_req_t> core_req;
  SimChannel<cache_rsp_t> core_rsp;
  SimChannel<mem_req_t>   mem_req;
  SimChannel<mem_rsp_t>   mem_rsp;

  Cache(const SimContext& ctx, const char* name, uint32_t capacity_bytes, uint32_t num_ways);
  ~Cache();

  void reset();
  void tick();

  const PerfStats& get_stats() const {
    return perf_stats_;
  }

private:
  struct tag_entry_t {
    bool     valid;
    uint64_t tag;
  };

  struct mshr_state_t {
    bool     valid;
    cache_req_t req;
    uint32_t set;
    uint32_t way;
    uint64_t tag;
  };

  uint32_t set_count() const;
  uint32_t set_index(uint64_t addr) const;
  uint64_t line_tag(uint64_t addr) const;
  uint32_t line_offset(uint64_t addr) const;

  bool lookup(uint64_t addr, uint32_t* set, uint32_t* way);
  uint32_t pick_victim_way(uint32_t set) const;
  void touch(uint32_t set, uint32_t way);

  void write_word_to_line(uint32_t set, uint32_t way, uint32_t offset, uint64_t byteen, uint32_t data);
  uint32_t read_word_from_line(uint32_t set, uint32_t way, uint32_t offset) const;

  std::vector<std::vector<tag_entry_t>> tag_store_;
  std::vector<std::vector<std::shared_ptr<mem_block_t>>> data_store_;
  mshr_state_t mshr_;
  bool pending_core_rsp_;
  cache_rsp_t pending_rsp_;
  uint32_t capacity_bytes_;
  uint32_t num_ways_;
  std::unique_ptr<ReplPolicy> repl_policy_;
  PerfStats perf_stats_;
};

} // namespace tinyrv
