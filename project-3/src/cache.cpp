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

#include "cache.h"
#include <cassert>
#include <cstring>
#include <vector>

using namespace tinyrv;

Cache::Cache(const SimContext& ctx, const char* name, uint32_t capacity_bytes, uint32_t num_ways)
  : SimObject<Cache>(ctx, name)
  , core_req(this)
  , core_rsp(this)
  , mem_req(this)
  , mem_rsp(this)
  , tag_store_(capacity_bytes / (MEM_BLOCK_SIZE * num_ways), std::vector<tag_entry_t>(num_ways))
  , data_store_(capacity_bytes / (MEM_BLOCK_SIZE * num_ways), std::vector<std::shared_ptr<mem_block_t>>(num_ways))
  , mshr_({false, {false, 0, 0, 0, 0}, 0, 0, 0})
  , pending_core_rsp_(false)
  , pending_rsp_({0, 0})
  , capacity_bytes_(capacity_bytes)
  , num_ways_(num_ways)
  , repl_policy_(std::make_unique<MyReplPolicy>())
  , perf_stats_() {
  assert(num_ways_ > 0);
  assert((capacity_bytes_ % (MEM_BLOCK_SIZE * num_ways_)) == 0);
  assert(this->set_count() > 0);
  repl_policy_->reset(this->set_count(), num_ways_);
  this->reset();
}

Cache::~Cache() {
  // --
}

void Cache::reset() {
  while (!mem_rsp.empty()) {
    mem_rsp.pop();
  }

  for (uint32_t s = 0; s < this->set_count(); ++s) {
    for (uint32_t w = 0; w < num_ways_; ++w) {
      tag_store_[s][w] = {false, 0};
      data_store_[s][w].reset();
    }
  }

  mshr_ = {false, {false, 0, 0, 0, 0}, 0, 0, 0};
  pending_core_rsp_ = false;
  pending_rsp_ = {0, 0};
  repl_policy_->reset(this->set_count(), num_ways_);
  perf_stats_ = PerfStats();
}

void Cache::tick() {
  // Step 1) If a core response is buffered, handle it first.
  if (pending_core_rsp_) {
    if (!core_rsp.full()) {
      core_rsp.send(pending_rsp_, 0);
      pending_core_rsp_ = false;
    }
    return;
  }

  // Step 2) If a miss is outstanding, wait for memory fill data.
  if (mshr_.valid) {
    if (!mem_rsp.empty()) {
      auto m = mem_rsp.peek();
      mem_rsp.pop();

      // Install fetched line into selected set/way.
      tag_store_[mshr_.set][mshr_.way] = {true, mshr_.tag};
      data_store_[mshr_.set][mshr_.way] = m.data;
      repl_policy_->on_fill(mshr_.set, mshr_.way);

      // Prepare deferred response back to core.
      uint32_t offset = this->line_offset(mshr_.req.addr);
      pending_rsp_ = {this->read_word_from_line(mshr_.set, mshr_.way, offset), mshr_.req.user};
      pending_core_rsp_ = true;
      mshr_.valid = false;
    }
    return;
  } else {
    if (!mem_rsp.empty()) {
      // Unexpected memory response without a pending miss context.
      std::abort();
    }
  }

  // Step 3) Process next core request.
  if (!core_req.empty()) {
    auto req = core_req.peek();
    uint32_t set = 0, way = 0;
    bool hit = this->lookup(req.addr, &set, &way);
    uint32_t offset = this->line_offset(req.addr);

    // Step 3a) Write path: send write-through request to memory.
    if (req.is_write) {
      mem_req_t wreq{};
      wreq.is_write = true;
      wreq.addr = req.addr;
      wreq.byteen = req.byteen;
      wreq.data = std::make_shared<mem_block_t>();
      std::memset(wreq.data->data(), 0, wreq.data->size());
      std::memcpy(wreq.data->data(), &req.data, sizeof(req.data));

      if (!mem_req.try_send(wreq, 0)) {
        return;
      }

      // Track write-side statistics.
      ++perf_stats_.core_writes;
      if (hit) {
        ++perf_stats_.write_hits;
      } else {
        ++perf_stats_.write_misses;
      }

      if (hit) {
        // Write-hit update in cache line (write-through + write-allocate behavior).
        this->write_word_to_line(set, way, offset, req.byteen, req.data);
        this->touch(set, way);
      }

      core_req.pop();
      return;
    }

    // Step 3b) Read hit: return data immediately.
    if (hit) {
      if (core_rsp.full()) {
        return;
      }

      ++perf_stats_.core_reads;
      ++perf_stats_.read_hits;

      uint32_t value = this->read_word_from_line(set, way, offset);
      this->touch(set, way);
      core_rsp.send({value, req.user}, 0);
      core_req.pop();
      return;
    }

    // Step 3c) Read miss: send memory line fetch and record miss context.
    uint32_t victim_way = this->pick_victim_way(set);
    uint64_t line_addr = req.addr & ~(uint64_t(MEM_BLOCK_SIZE - 1));

    mem_req_t mreq{};
    mreq.is_write = false;
    mreq.addr = line_addr;
    mreq.byteen = 0;
    mreq.data = nullptr;

    if (!mem_req.try_send(mreq, 0)) {
      return;
    }

    ++perf_stats_.core_reads;
    ++perf_stats_.read_misses;

    mshr_ = {true, req, set, victim_way, this->line_tag(req.addr)};
    core_req.pop();
  }
}

uint32_t Cache::set_count() const {
  return capacity_bytes_ / (MEM_BLOCK_SIZE * num_ways_);
}

uint32_t Cache::set_index(uint64_t addr) const {
  return (addr / MEM_BLOCK_SIZE) % this->set_count();
}

uint64_t Cache::line_tag(uint64_t addr) const {
  return (addr / MEM_BLOCK_SIZE) / this->set_count();
}

uint32_t Cache::line_offset(uint64_t addr) const {
  return addr % MEM_BLOCK_SIZE;
}

bool Cache::lookup(uint64_t addr, uint32_t* set, uint32_t* way) {
  // Set+tag lookup in all ways of the indexed set.
  uint32_t s = this->set_index(addr);
  uint64_t t = this->line_tag(addr);

  for (uint32_t w = 0; w < num_ways_; ++w) {
    auto& e = tag_store_[s][w];
    if (e.valid && e.tag == t) {
      if (set) *set = s;
      if (way) *way = w;
      return true;
    }
  }

  if (set) *set = s;
  if (way) *way = 0;
  return false;
}

uint32_t Cache::pick_victim_way(uint32_t set) const {
  std::vector<bool> valid_ways(num_ways_, false);
  for (uint32_t w = 0; w < num_ways_; ++w) {
    valid_ways[w] = tag_store_[set][w].valid;
  }
  return repl_policy_->pick_victim(set, valid_ways);
}

void Cache::touch(uint32_t set, uint32_t way) {
  repl_policy_->on_access(set, way);
}

void Cache::write_word_to_line(uint32_t set, uint32_t way, uint32_t offset, uint64_t byteen, uint32_t data) {
  // Byte-enable write into one 32-bit word within the cache line.
  if (offset + sizeof(uint32_t) > MEM_BLOCK_SIZE)
    return;

  if (!data_store_[set][way]) {
    data_store_[set][way] = std::make_shared<mem_block_t>();
    std::memset(data_store_[set][way]->data(), 0, data_store_[set][way]->size());
  }

  for (uint32_t i = 0; i < sizeof(uint32_t); ++i) {
    if (byteen & (1ull << i)) {
      (*data_store_[set][way])[offset + i] = (data >> (i * 8)) & 0xff;
    }
  }
}

uint32_t Cache::read_word_from_line(uint32_t set, uint32_t way, uint32_t offset) const {
  // Read one 32-bit word from a cache line.
  if (offset + sizeof(uint32_t) > MEM_BLOCK_SIZE)
    return 0;

  if (!data_store_[set][way])
    return 0;

  uint32_t value = 0;
  for (uint32_t i = 0; i < sizeof(uint32_t); ++i) {
    value |= uint32_t((*data_store_[set][way])[offset + i]) << (i * 8);
  }
  return value;
}
