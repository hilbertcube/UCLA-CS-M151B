#pragma once

#include <cassert>
#include <cstdint>
#include <vector>

namespace tinyrv
{

	class ReplPolicy
	{
	public:
		virtual ~ReplPolicy() = default;

		virtual void reset(uint32_t set_count, uint32_t num_ways) = 0;

		// Selects a victim way for a set. The vector indicates if each way is valid.
		virtual uint32_t pick_victim(uint32_t set, const std::vector<bool> &valid_ways) const = 0;

		// Called when a line is hit (read or write hit).
		virtual void on_access(uint32_t set, uint32_t way) = 0;

		// Called when a new line is installed in the cache.
		virtual void on_fill(uint32_t set, uint32_t way) = 0;
	};

	// Default LRU policy.
	class LRUReplPolicy : public ReplPolicy
	{
	public:
		LRUReplPolicy()
			: use_counter_(1)
		{
		}

		void reset(uint32_t set_count, uint32_t num_ways) override
		{
			assert(set_count > 0);
			assert(num_ways > 0);
			timestamps_.assign(set_count, std::vector<uint64_t>(num_ways, 0));
			use_counter_ = 1;
		}

		uint32_t pick_victim(uint32_t set, const std::vector<bool> &valid_ways) const override
		{
			// Prefer invalid way first.
			for (uint32_t w = 0; w < valid_ways.size(); ++w)
			{
				if (!valid_ways[w])
				{
					return w;
				}
			}

			// Otherwise choose least-recently-used way.
			uint32_t victim = 0;
			uint64_t min_used = timestamps_[set][0];
			for (uint32_t w = 1; w < valid_ways.size(); ++w)
			{
				if (timestamps_[set][w] < min_used)
				{
					min_used = timestamps_[set][w];
					victim = w;
				}
			}
			return victim;
		}

		void on_access(uint32_t set, uint32_t way) override
		{
			timestamps_[set][way] = use_counter_++;
		}

		void on_fill(uint32_t set, uint32_t way) override
		{
			timestamps_[set][way] = use_counter_++;
		}

	private:
		std::vector<std::vector<uint64_t>> timestamps_;
		uint64_t use_counter_;
	};

// SHIP (Signature-based Hit Predictor) replacement policy
	// Combines SRRIP with signature-based learning to predict reuse.
	class MyReplPolicy : public ReplPolicy
	{
	public:
		static constexpr uint32_t RRPV_MAX = 3;		 // 2-bit RRPV
		static constexpr uint32_t SHCT_SIZE = 16384; // Signature History Counter Table size
		static constexpr uint32_t SHCT_MAX = 7;		 // 3-bit saturating counter max

		MyReplPolicy() {}
		~MyReplPolicy() {}

		void reset(uint32_t set_count, uint32_t num_ways) override
		{
			rrpv_.assign(set_count, std::vector<uint32_t>(num_ways, RRPV_MAX));
			signatures_.assign(set_count, std::vector<uint32_t>(num_ways, 0));
			outcome_.assign(set_count, std::vector<bool>(num_ways, false));
			shct_.assign(SHCT_SIZE, 0);
		}

		uint32_t pick_victim(uint32_t set, const std::vector<bool> &valid_ways) const override
		{
			// Prefer invalid way first.
			for (uint32_t w = 0; w < valid_ways.size(); ++w)
			{
				if (!valid_ways[w])
				{
					return w;
				}
			}

			// Find a way with RRPV == RRPV_MAX (distant re-reference)
			while (true)
			{
				for (uint32_t w = 0; w < valid_ways.size(); ++w)
				{
					if (rrpv_[set][w] == RRPV_MAX)
					{
						return w;
					}
				}
				// Age all entries in this set
				for (uint32_t w = 0; w < valid_ways.size(); ++w)
				{
					if (rrpv_[set][w] < RRPV_MAX)
					{
						++const_cast<std::vector<std::vector<uint32_t>> &>(rrpv_)[set][w];
					}
				}
			}
		}

		void on_access(uint32_t set, uint32_t way) override
		{
			// Hit: set RRPV to 0 (near-immediate re-reference)
			rrpv_[set][way] = 0;
			// Mark outcome as true (was reused)
			if (!outcome_[set][way])
			{
				outcome_[set][way] = true;
				uint32_t sig = signatures_[set][way] % SHCT_SIZE;
				if (shct_[sig] < SHCT_MAX)
				{
					++shct_[sig];
				}
			}
		}

		void on_fill(uint32_t set, uint32_t way) override
		{
			// On eviction of the old line, decrement SHCT if it was not reused
			if (!outcome_[set][way])
			{
				uint32_t old_sig = signatures_[set][way] % SHCT_SIZE;
				if (shct_[old_sig] > 0)
				{
					--shct_[old_sig];
				}
			}

			// Compute signature for the new line (use set index as signature)
			uint32_t new_sig = set % SHCT_SIZE;
			signatures_[set][way] = new_sig;
			outcome_[set][way] = false;

			// If SHCT predicts reuse, insert with RRPV = RRPV_MAX - 1 (long re-reference)
			// Otherwise, insert with RRPV = RRPV_MAX (distant, easy to evict)
			if (shct_[new_sig] > 0)
			{
				rrpv_[set][way] = RRPV_MAX - 1;
			}
			else
			{
				rrpv_[set][way] = RRPV_MAX;
			}
		}

	private:
		std::vector<std::vector<uint32_t>> rrpv_;		// Re-Reference Prediction Value
		std::vector<std::vector<uint32_t>> signatures_; // Signature per cache line
		std::vector<std::vector<bool>> outcome_;		// Was this line reused?
		std::vector<uint32_t> shct_;					// Signature History Counter Table
	};

} // namespace tinyrv