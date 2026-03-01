//
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
//

#include "core.h"
#include "debug.h"
#include "types.h"
#include <assert.h>
#include <iostream>
#include <util.h>

using namespace tinyrv;

////////////////////////////////////////////////////////////////////////////////

GShare::GShare(uint32_t BTB_size, uint32_t BHR_size)
    : BTB_(BTB_size, BTB_entry_t{false, 0x0, 0x0})
    , PHT_((1 << BHR_size), 0x0), BHR_(0x0)
    , BTB_shift_(log2ceil(BTB_size))
    , BTB_mask_(BTB_size - 1)
    , BHR_mask_((1 << BHR_size) - 1)
{
  //--
}

GShare::~GShare()
{
  //-- destructor
}

uint32_t GShare::predict(uint32_t PC)
{
  uint32_t next_PC = PC + 4;
  bool predict_taken = false;

  // btb index from lower PC bits, pht index from PC xor BHR
  uint32_t btb_idx = (PC >> 2) & BTB_mask_;
  uint32_t pht_idx = ((PC >> 2) ^ BHR_) & BHR_mask_;

  // >=2 means taken
  predict_taken = (PHT_[pht_idx] >= 2);

  if (predict_taken && BTB_[btb_idx].valid && BTB_[btb_idx].tag == PC)
    next_PC = BTB_[btb_idx].target;

  DT(3, "*** GShare: predict PC=0x"
      << std::hex << PC << std::dec << ", next_PC=0x" << std::hex
      << next_PC << std::dec << ", predict_taken=" << predict_taken);
  return next_PC;
}

void GShare::update(uint32_t PC, uint32_t next_PC, bool taken)
{
  DT(3, "*** GShare: update PC=0x" << std::hex << PC << std::dec
      << ", next_PC=0x" << std::hex << next_PC
      << std::dec << ", taken=" << taken);

  uint32_t btb_idx = (PC >> 2) & BTB_mask_;
  uint32_t pht_idx = ((PC >> 2) ^ BHR_) & BHR_mask_;

  // update BTB on taken
  if (taken)
  {
    BTB_[btb_idx].valid = true;
    BTB_[btb_idx].tag = PC;
    BTB_[btb_idx].target = next_PC;
  }

  // bump 2-bit counter
  if (taken && PHT_[pht_idx] < 3)
    PHT_[pht_idx]++;
  else if (!taken && PHT_[pht_idx] > 0)
    PHT_[pht_idx]--;

  // shift outcome into BHR
  BHR_ = ((BHR_ << 1) | (taken ? 1u : 0u)) & BHR_mask_;
}

///////////////////////////////////////////////////////////////////////////////

// table sizes and history lengths for TAGE
static const uint32_t TAGE_SIZES[] = {1024, 1024, 1024, 512, 512};
static const uint32_t TAGE_HISTS[] = {4, 8, 16, 32, 64};

GSharePlus::GSharePlus(uint32_t BTB_size, uint32_t BHR_size)
    : BTB_(BTB_size, BTB_entry_t{false, 0x0, 0x0})
    , BTB_mask_(BTB_size - 1)
    , base_(BASE_SIZE, 0)
    , ghist_(0), phist_(0)
    , reset_ctr_(0)
    , reset_phase_(false)
{
  (void)BHR_size;
  for (int i = 0; i < NTABLES; i++)
  {
    tsize_[i] = TAGE_SIZES[i];
    tmask_[i] = TAGE_SIZES[i] - 1;
    thist_[i] = TAGE_HISTS[i];
    // init tags to 0xFFFF to avoid false hits on cold start
    table_[i].resize(TAGE_SIZES[i], TagEntry{0, 0xFFFF, 0});
  }
}

GSharePlus::~GSharePlus()
{
  //--
}

// xor-fold srclen bits down to dstlen bits
uint32_t GSharePlus::fold(uint64_t val, int srclen, int dstlen) const
{
  if (srclen <= 0 || dstlen <= 0)
    return 0;
  uint64_t masked = (srclen >= 64) ? val : (val & ((1ULL << srclen) - 1));
  uint32_t result = 0;
  uint32_t dmask = (dstlen >= 32) ? 0xFFFFFFFFu : ((1u << dstlen) - 1);
  for (int i = 0; i < srclen; i += dstlen)
  {
    result ^= (uint32_t)((masked >> i) & dmask);
  }
  return result & dmask;
}

uint32_t GSharePlus::gindex(uint32_t pc, int t) const
{
  uint32_t pc_idx = pc >> 2;
  int idx_bits = log2ceil(tsize_[t]);
  uint32_t h = fold(ghist_, thist_[t], idx_bits);
  h ^= fold((uint64_t)phist_, 16, idx_bits);
  return (pc_idx ^ h) & tmask_[t];
}

uint16_t GSharePlus::gtag(uint32_t pc, int t) const
{
  uint32_t pc_idx = pc >> 2;
  uint32_t h1 = fold(ghist_, thist_[t], TAG_BITS);
  uint32_t h2 = fold(ghist_, thist_[t], TAG_BITS - 1);
  return (uint16_t)((pc_idx ^ h1 ^ (h2 << 1)) & ((1u << TAG_BITS) - 1));
}

GSharePlus::PredInfo GSharePlus::compute_pred(uint32_t PC) const
{
  PredInfo info;
  info.provider = -1;
  info.altprovider = -1;
  uint32_t pc_idx = (PC >> 2);

  // start with bimodal
  bool base_pred = (base_[pc_idx & (BASE_SIZE - 1)] >= 2);
  info.alt_pred = base_pred;
  info.provider_pred = base_pred;

  // check tagged tables, longest history first
  for (int t = NTABLES - 1; t >= 0; t--)
  {
    uint32_t idx = gindex(PC, t);
    if (table_[t][idx].tag == gtag(PC, t))
    {
      if (info.provider < 0)
      {
        info.provider = t;
        info.provider_pred = (table_[t][idx].ctr >= 0);
      }
      else if (info.altprovider < 0)
      {
        info.altprovider = t;
        info.alt_pred = (table_[t][idx].ctr >= 0);
        break;
      }
    }
  }

  // use altpred when provider entry looks newly allocated (weak ctr, useful==0)
  if (info.provider >= 0)
  {
    auto &pentry = table_[info.provider][gindex(PC, info.provider)];
    if ((pentry.ctr == 0 || pentry.ctr == -1) && pentry.useful == 0)
    {
      info.pred = info.alt_pred;
    }
    else
    {
      info.pred = info.provider_pred;
    }
  }
  else
  {
    info.pred = base_pred;
  }

  return info;
}

uint32_t GSharePlus::predict(uint32_t PC)
{
  uint32_t next_PC = PC + 4;
  uint32_t pc_idx = (PC >> 2);

  auto info = compute_pred(PC);
  bool predict_taken = info.pred;

  // check BTB
  uint32_t btb_idx = pc_idx & BTB_mask_;
  if (predict_taken && BTB_[btb_idx].valid && BTB_[btb_idx].tag == PC)
  {
    next_PC = BTB_[btb_idx].target;
  }

  // TODO: extra credit component

  DT(3, "*** GShare+: predict PC=0x" << std::hex << PC << std::dec
      << ", next_PC=0x" << std::hex << next_PC << std::dec
      << ", predict_taken=" << predict_taken);
  return next_PC;
}

void GSharePlus::update(uint32_t PC, uint32_t next_PC, bool taken)
{
  uint32_t pc_idx = (PC >> 2);
  auto info = compute_pred(PC);

  // update provider counter
  if (info.provider >= 0)
  {
    uint32_t idx = gindex(PC, info.provider);
    auto &entry = table_[info.provider][idx];
    if (taken && entry.ctr < 3)
      entry.ctr++;
    else if (!taken && entry.ctr > -4)
      entry.ctr--;

    // useful tracking: only matters when provider and alt disagree
    if (info.provider_pred != info.alt_pred)
    {
      if (info.provider_pred == taken)
      {
        if (entry.useful < 3)
          entry.useful++;
      }
      else
      {
        if (entry.useful > 0)
          entry.useful--;
      }
    }
  }

  // always update bimodal too
  uint32_t base_idx = pc_idx & (BASE_SIZE - 1);
  if (taken && base_[base_idx] < 3)
    base_[base_idx]++;
  else if (!taken && base_[base_idx] > 0)
    base_[base_idx]--;

  // on mispredict, try to allocate in a longer-history table
  if (info.pred != taken)
  {
    int start = (info.provider >= 0) ? (info.provider + 1) : 0;
    bool allocated = false;

    // grab first entry with useful==0
    for (int t = start; t < NTABLES && !allocated; t++)
    {
      uint32_t idx = gindex(PC, t);
      if (table_[t][idx].useful == 0)
      {
        table_[t][idx].tag = gtag(PC, t);
        table_[t][idx].ctr = taken ? 0 : -1;
        table_[t][idx].useful = 0;
        allocated = true;
      }
    }

    // no room? age out usefulness so we can allocate next time
    if (!allocated)
    {
      for (int t = start; t < NTABLES; t++)
      {
        uint32_t idx = gindex(PC, t);
        if (table_[t][idx].useful > 0)
          table_[t][idx].useful--;
      }
    }
  }

  // periodically reset useful bits (every 256K branches)
  reset_ctr_++;
  if (reset_ctr_ == (1u << 18))
  {
    reset_ctr_ = 0;
    reset_phase_ = !reset_phase_;
    for (int t = 0; t < NTABLES; t++)
    {
      for (auto &e : table_[t])
      {
        e.useful = reset_phase_ ? (e.useful & 1) : (e.useful & 2);
      }
    }
  }

  // shift in new history bits
  ghist_ = (ghist_ << 1) | (taken ? 1ULL : 0ULL);
  phist_ = ((phist_ << 1) | ((PC >> 2) & 1)) & 0xFFFF;

  // update BTB
  uint32_t btb_idx = pc_idx & BTB_mask_;
  if (taken)
  {
    BTB_[btb_idx].valid = true;
    BTB_[btb_idx].tag = PC;
    BTB_[btb_idx].target = next_PC;
  }

  // TODO: extra credit component

  DT(3, "*** GShare+: update PC=0x" << std::hex << PC << std::dec
        << ", next_PC=0x" << std::hex << next_PC << std::dec
        << ", taken=" << taken);
}
