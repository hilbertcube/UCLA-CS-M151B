#pragma once

#include <cassert>
#include <cstdint>
#include <vector>

namespace tinyrv {

class ReplPolicy {
public:
	virtual ~ReplPolicy() = default;

	virtual void reset(uint32_t set_count, uint32_t num_ways) = 0;

	// Selects a victim way for a set. The vector indicates if each way is valid.
	virtual uint32_t pick_victim(uint32_t set, const std::vector<bool>& valid_ways) const = 0;

	// Called when a line is hit (read or write hit).
	virtual void on_access(uint32_t set, uint32_t way) = 0;

	// Called when a new line is installed in the cache.
	virtual void on_fill(uint32_t set, uint32_t way) = 0;
};

// Default LRU policy.
class LRUReplPolicy : public ReplPolicy {
public:
	LRUReplPolicy()
		: use_counter_(1)
	{}

	void reset(uint32_t set_count, uint32_t num_ways) override {
		assert(set_count > 0);
		assert(num_ways > 0);
		timestamps_.assign(set_count, std::vector<uint64_t>(num_ways, 0));
		use_counter_ = 1;
	}

	uint32_t pick_victim(uint32_t set, const std::vector<bool>& valid_ways) const override {
		// Prefer invalid way first.
		for (uint32_t w = 0; w < valid_ways.size(); ++w) {
			if (!valid_ways[w]) {
				return w;
			}
		}

		// Otherwise choose least-recently-used way.
		uint32_t victim = 0;
		uint64_t min_used = timestamps_[set][0];
		for (uint32_t w = 1; w < valid_ways.size(); ++w) {
			if (timestamps_[set][w] < min_used) {
				min_used = timestamps_[set][w];
				victim = w;
			}
		}
		return victim;
	}

	void on_access(uint32_t set, uint32_t way) override {
		timestamps_[set][way] = use_counter_++;
	}

	void on_fill(uint32_t set, uint32_t way) override {
		timestamps_[set][way] = use_counter_++;
	}

private:
	std::vector<std::vector<uint64_t>> timestamps_;
	uint64_t use_counter_;
};


#define MyReplPolicy LRUReplPolicy
// Comment the above line out if you would like to attempt extra credits
// TODO: Implement your own replacement policy
// class MyReplPolicy : public ReplPolicy {
// public:
// 	MyReplPolicy() {}
// 	~MyReplPolicy() {}

// 	void reset(uint32_t set_count, uint32_t num_ways) override {
// 		// TODO:
// 	}

// 	uint32_t pick_victim(uint32_t set, const std::vector<bool>& valid_ways) const override {
// 		// TODO:
// 	}

// 	void on_access(uint32_t set, uint32_t way) override {
// 		// TODO:
// 	}

// 	void on_fill(uint32_t set, uint32_t way) override {
// 		// TODO:
// 	}
// };


} // namespace tinyrv