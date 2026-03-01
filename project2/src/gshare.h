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

namespace tinyrv
{

  // BTB entry
  struct BTB_entry_t
  {
    bool valid;
    uint32_t tag;
    uint32_t target;
  };

  class BranchPredictor
  {
  public:
    virtual ~BranchPredictor() {}

    virtual uint32_t predict(uint32_t PC) { return PC + 4; };

    virtual void update(uint32_t PC, uint32_t next_PC, bool taken)
    {
      (void)PC;
      (void)next_PC;
      (void)taken;
    };
  };

  class GShare : public BranchPredictor
  {
  public:
    GShare(uint32_t BTB_size, uint32_t BHR_size);

    ~GShare() override;

    uint32_t predict(uint32_t PC) override;
    void update(uint32_t PC, uint32_t next_PC, bool taken) override;

    // TODO: Add your own methods here
  private:
    std::vector<BTB_entry_t> BTB_;
    std::vector<uint8_t> PHT_; // 2-bit counters
    uint32_t BHR_;             // branch history reg
    uint32_t BTB_shift_;
    uint32_t BTB_mask_;
    uint32_t BHR_mask_;
  };

  class GSharePlus : public BranchPredictor
  {
  public:
    GSharePlus(uint32_t BTB_size, uint32_t BHR_size);

    ~GSharePlus() override;

    uint32_t predict(uint32_t PC) override;
    void update(uint32_t PC, uint32_t next_PC, bool taken) override;

    // TODO: extra credit component
  private:
    // BTB
    std::vector<BTB_entry_t> BTB_;
    uint32_t BTB_mask_;

    // TAGE predictor
    enum
    {
      NTABLES = 5,
      BASE_SIZE = 4096,
      TAG_BITS = 10
    };

    // bimodal base table
    std::vector<uint8_t> base_; // 2-bit counters

    struct TagEntry
    {
      int8_t ctr;     // 3-bit signed counter (-4..3)
      uint16_t tag;   // partial tag
      uint8_t useful; // usefulness (0..3)
    };

    // tagged tables (geometric history lengths)
    std::vector<TagEntry> table_[NTABLES];
    uint32_t tsize_[NTABLES];
    uint32_t tmask_[NTABLES];
    uint32_t thist_[NTABLES];

    uint64_t ghist_; // global history
    uint32_t phist_; // path history

    // for periodic useful-bit reset
    uint32_t reset_ctr_;
    bool reset_phase_;

    // saved prediction state so update() can reuse it
    struct PredInfo
    {
      int provider;       // longest-history hit (-1 = base)
      int altprovider;    // 2nd longest hit (-1 = base)
      bool pred;          // final prediction
      bool provider_pred; // what provider says
      bool alt_pred;      // what altpred says
    };
    PredInfo compute_pred(uint32_t PC) const;

    // hashing helpers
    uint32_t fold(uint64_t val, int srclen, int dstlen) const;
    uint32_t gindex(uint32_t pc, int t) const;
    uint16_t gtag(uint32_t pc, int t) const;
  };

} // namespace tinyrv
