#include "BP.h"
#include <util.h>

BP::BP()
  : next_PC_(0)
{
  this->reset();
}

BP::~BP() {
  //--
}

void BP::reset() {
  next_PC_ = 0;
  stats_ = PerfStats();
}

bool BP::predict(uint32_t PC) {
  // Not-Taken predictor
  next_PC_ = PC + 4;
  return false;
}

void BP::update(uint32_t PC, bool pred_taken, uint32_t pred_target,
                bool actual_taken, uint32_t actual_target) {
  __unused(PC);
  ++stats_.direction_total;
  ++stats_.target_total;

  if (pred_taken == actual_taken) {
    ++stats_.direction_correct;
  }

  if (pred_target == actual_target) {
    ++stats_.target_correct;
  } else {
    ++stats_.mispredictions;
  }
}