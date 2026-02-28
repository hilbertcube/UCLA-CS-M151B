//
// Copyright 2026 Blaise Tine
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once

#include <cstdint>
#include <vector>

namespace tinyrv {

// BTB entry: valid bit, PC tag (for match), and target address
struct BTB_entry_t {
  bool valid;
  uint32_t tag;
  uint32_t target;
};

class BranchPredictor {
public:
  virtual ~BranchPredictor() {}

  virtual uint32_t predict(uint32_t PC) { return PC + 4; };

  virtual void update(uint32_t PC, uint32_t next_PC, bool taken) {
    (void)PC;
    (void)next_PC;
    (void)taken;
  };
};

class GShare : public BranchPredictor {
public:
  GShare(uint32_t BTB_size, uint32_t BHR_size);

  ~GShare() override;

  uint32_t predict(uint32_t PC) override;
  void update(uint32_t PC, uint32_t next_PC, bool taken) override;

  // TODO: Add your own methods here
private:
  std::vector<BTB_entry_t> BTB_;
  std::vector<uint8_t> PHT_; // 2-bit saturating counters (0–3)
  uint32_t BHR_;             // branch history register
  uint32_t BTB_shift_;
  uint32_t BTB_mask_;
  uint32_t BHR_mask_;
};

class GSharePlus : public BranchPredictor {
public:
  GSharePlus(uint32_t BTB_size, uint32_t BHR_size);

  ~GSharePlus() override;

  uint32_t predict(uint32_t PC) override;
  void update(uint32_t PC, uint32_t next_PC, bool taken) override;

  // TODO: extra credit component
private:
  // BTB (shared)
  std::vector<BTB_entry_t> BTB_;
  uint32_t BTB_mask_;

  // Global predictor (GShare): GHR XOR PC indexes into global PHT
  std::vector<uint8_t> global_PHT_; // 2-bit saturating counters
  uint32_t GHR_;                    // global history register
  uint32_t GHR_mask_;

  // Local predictor: per-branch local history table -> local PHT
  std::vector<uint32_t> local_HT_; // local history table (per-PC histories)
  std::vector<uint8_t> local_PHT_; // 2-bit saturating counters
  uint32_t local_HT_mask_;
  uint32_t local_PHT_mask_;

  // Meta/chooser table: 2-bit counters; 0,1 -> global; 2,3 -> local
  std::vector<uint8_t> meta_;
  uint32_t meta_mask_;
};

} // namespace tinyrv
