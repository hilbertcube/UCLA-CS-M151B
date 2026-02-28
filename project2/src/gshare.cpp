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
    , PHT_((1 << BHR_size), 0x0)
    , BHR_(0x0), BTB_shift_(log2ceil(BTB_size))
    , BTB_mask_(BTB_size - 1)
    , BHR_mask_((1 << BHR_size) - 1) {
  //--
}

GShare::~GShare() {
  //-- destructor
}

uint32_t GShare::predict(uint32_t PC) {
  uint32_t next_PC = PC + 4;
  bool predict_taken = false;

  // Index BTB by lower bits of (PC >> 2); index PHT by (PC >> 2) XOR BHR
  // (gshare)
  uint32_t btb_idx = (PC >> 2) & BTB_mask_;
  uint32_t pht_idx = ((PC >> 2) ^ BHR_) & BHR_mask_;

  // 2-bit counter: 0,1 -> not taken; 2,3 -> taken
  predict_taken = (PHT_[pht_idx] >= 2);

  if (predict_taken && BTB_[btb_idx].valid && BTB_[btb_idx].tag == PC)
    next_PC = BTB_[btb_idx].target;

  DT(3, "*** GShare: predict PC=0x"
            << std::hex << PC << std::dec << ", next_PC=0x" << std::hex
            << next_PC << std::dec << ", predict_taken=" << predict_taken);
  return next_PC;
}

void GShare::update(uint32_t PC, uint32_t next_PC, bool taken) {
  DT(3, "*** GShare: update PC=0x" << std::hex << PC << std::dec
                                   << ", next_PC=0x" << std::hex << next_PC
                                   << std::dec << ", taken=" << taken);

  uint32_t btb_idx = (PC >> 2) & BTB_mask_;
  uint32_t pht_idx = ((PC >> 2) ^ BHR_) & BHR_mask_;

  // Update BTB: store this branch's target
  if (taken) {
    BTB_[btb_idx].valid = true;
    BTB_[btb_idx].tag = PC;
    BTB_[btb_idx].target = next_PC;
  }

  // Update 2-bit saturating counter (0–3)
  if (taken && PHT_[pht_idx] < 3)
    PHT_[pht_idx]++;
  else if (!taken && PHT_[pht_idx] > 0)
    PHT_[pht_idx]--;

  // Update BHR: shift in the outcome (1 = taken, 0 = not taken)
  BHR_ = ((BHR_ << 1) | (taken ? 1u : 0u)) & BHR_mask_;
}

///////////////////////////////////////////////////////////////////////////////

GSharePlus::GSharePlus(uint32_t BTB_size, uint32_t BHR_size)
    : BTB_(BTB_size, BTB_entry_t{false, 0x0, 0x0}), BTB_mask_(BTB_size - 1),
      global_PHT_((1 << BHR_size), 0), GHR_(0), GHR_mask_((1 << BHR_size) - 1),
      local_HT_((1 << BHR_size), 0), local_PHT_((1 << BHR_size), 0),
      local_HT_mask_((1 << BHR_size) - 1), local_PHT_mask_((1 << BHR_size) - 1),
      meta_((1 << BHR_size), 0) // start biased toward global
      ,
      meta_mask_((1 << BHR_size) - 1) {
  //--
}

GSharePlus::~GSharePlus() {
  //--
}

uint32_t GSharePlus::predict(uint32_t PC) {
  uint32_t next_PC = PC + 4;

  uint32_t pc_idx = (PC >> 2);

  // Global prediction (GShare): index = PC XOR GHR
  uint32_t g_idx = (pc_idx ^ GHR_) & GHR_mask_;
  bool g_taken = (global_PHT_[g_idx] >= 2);

  // Local prediction: local history table indexed by PC, then local PHT
  uint32_t lht_idx = pc_idx & local_HT_mask_;
  uint32_t l_idx = local_HT_[lht_idx] & local_PHT_mask_;
  bool l_taken = (local_PHT_[l_idx] >= 2);

  // Meta/chooser: 0,1 -> use global; 2,3 -> use local
  uint32_t m_idx = (pc_idx ^ GHR_) & meta_mask_;
  bool use_local = (meta_[m_idx] >= 2);

  bool predict_taken = use_local ? l_taken : g_taken;

  // BTB lookup
  uint32_t btb_idx = pc_idx & BTB_mask_;
  if (predict_taken && BTB_[btb_idx].valid && BTB_[btb_idx].tag == PC) {
    next_PC = BTB_[btb_idx].target;
  }

  // TODO: extra credit component

  DT(3, "*** GShare+: predict PC=0x"
    << std::hex << PC << std::dec << ", next_PC=0x" << std::hex
    << next_PC << std::dec << ", predict_taken=" << predict_taken
    << ", use_local=" << use_local);
  return next_PC;
}

void GSharePlus::update(uint32_t PC, uint32_t next_PC, bool taken) {
  uint32_t pc_idx = (PC >> 2);

  // Recompute indices (same as predict)
  uint32_t g_idx = (pc_idx ^ GHR_) & GHR_mask_;
  bool g_taken = (global_PHT_[g_idx] >= 2);

  uint32_t lht_idx = pc_idx & local_HT_mask_;
  uint32_t l_idx = local_HT_[lht_idx] & local_PHT_mask_;
  bool l_taken = (local_PHT_[l_idx] >= 2);

  uint32_t m_idx = (pc_idx ^ GHR_) & meta_mask_;

  // Update meta/chooser: if predictors disagree, reward the correct one
  if (g_taken != l_taken) {
    if (l_taken == taken && meta_[m_idx] < 3)
      meta_[m_idx]++;
    else if (g_taken == taken && meta_[m_idx] > 0)
      meta_[m_idx]--;
  }

  // Update global PHT (2-bit saturating counter)
  if (taken && global_PHT_[g_idx] < 3)
    global_PHT_[g_idx]++;
  else if (!taken && global_PHT_[g_idx] > 0)
    global_PHT_[g_idx]--;

  // Update local PHT (2-bit saturating counter)
  if (taken && local_PHT_[l_idx] < 3)
    local_PHT_[l_idx]++;
  else if (!taken && local_PHT_[l_idx] > 0)
    local_PHT_[l_idx]--;

  // Update local history table: shift in the outcome
  local_HT_[lht_idx] = ((local_HT_[lht_idx] << 1) | (taken ? 1u : 0u)) & local_PHT_mask_;

  // Update GHR: shift in the outcome
  GHR_ = ((GHR_ << 1) | (taken ? 1u : 0u)) & GHR_mask_;

  // Update BTB
  uint32_t btb_idx = pc_idx & BTB_mask_;
  if (taken) {
    BTB_[btb_idx].valid = true;
    BTB_[btb_idx].tag = PC;
    BTB_[btb_idx].target = next_PC;
  }

  // TODO: extra credit component

  DT(3, "*** GShare+: update PC=0x" 
    << std::hex << PC << std::dec 
    << ", next_PC=0x" << std::hex << next_PC
    << std::dec << ", taken=" << taken);
}
