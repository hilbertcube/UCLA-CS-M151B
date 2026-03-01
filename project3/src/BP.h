// BP.h
#pragma once

#include <cstdint>

class BP {
public:

  struct PerfStats {
    uint64_t direction_correct;
    uint64_t direction_total;
    uint64_t target_correct;
    uint64_t target_total;
    uint64_t mispredictions;

    PerfStats()
      : direction_correct(0)
      , direction_total(0)
      , target_correct(0)
      , target_total(0)
      , mispredictions(0)
    {}

    double direction_accuracy() const {
      if (direction_total == 0)
        return 0.0;
      return double(direction_correct) / double(direction_total);
    }

    double target_accuracy() const {
      if (target_total == 0)
        return 0.0;
      return double(target_correct) / double(target_total);
    }

    double mpki(uint64_t committed_instrs) const {
      if (committed_instrs == 0)
        return 0.0;
      return (1000.0 * double(mispredictions)) / double(committed_instrs);
    }
  };

  BP();
  ~BP();

  void reset();

  bool predict(unsigned PC);
  void update(unsigned PC, bool pred_taken, uint32_t pred_target,
              bool actual_taken, uint32_t actual_target);

  uint32_t get_nextPC() const {
    return next_PC_;
  }

  const PerfStats& get_stats() const {
    return stats_;
  }

private:
  uint32_t next_PC_;
  PerfStats stats_;
};
