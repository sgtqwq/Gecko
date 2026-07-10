#ifndef SEARCH_H
#define SEARCH_H

#include "types.h"
#include "position.h"
#include "tt.h"
#include <atomic>
#include <chrono>
#include <algorithm>

constexpr i32 INF = 30000;
constexpr i32 MATE_SCORE = 29000;
constexpr i32 MAX_PLY = 256;
constexpr i32 MAX_MOVES = 256;

// History heuristic constants
constexpr i32 HISTORY_MAX = 16384;
constexpr i32 HISTORY_BONUS_MAX = 2000;

// Killer move constants
constexpr i32 NUM_KILLERS = 2;

// History tables
struct HistoryTable {
	i32 quiet_history[2][64][64];  // [color][from][to]
	
	HistoryTable() {
		clear();
	}
	
	void clear() {
		for (int c = 0; c < 2; ++c) {
			for (int from = 0; from < 64; ++from) {
				for (int to = 0; to < 64; ++to) {
					quiet_history[c][from][to] = 0;
				}
			}
		}
	}
	
	// Get history score for a quiet move
	i32 get_score(bool color, i32 from, i32 to) const {
		return quiet_history[color ? 1 : 0][from][to];
	}
	
	// Update history score with bonus/penalty
	void update(bool color, i32 from, i32 to, i32 bonus) {
		i32& score = quiet_history[color ? 1 : 0][from][to];
		// Gravity-based update to prevent overflow
		score += bonus - score * abs(bonus) / HISTORY_MAX;
		score = std::clamp(score, -HISTORY_MAX, HISTORY_MAX);
	}
};

struct SearchInfo {
	u64 nodes;
	i32 depth;
	i32 seldepth;
	Move pv[MAX_PLY];
	i32 pv_length;
	
	std::chrono::steady_clock::time_point start_time;
	i64 soft_time_limit;
	i64 time_limit;
	bool infinite;
	
	SearchInfo()
	: nodes(0), depth(0), seldepth(0), pv_length(0),
	soft_time_limit(0), time_limit(0), infinite(true) {}
	
	void reset() {
		nodes = 0;
		depth = 0;
		seldepth = 0;
		pv_length = 0;
	}
	
	i64 elapsed_time() const {
		auto now = std::chrono::steady_clock::now();
		return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
	}
};

namespace Search {
	extern std::atomic<bool> stopped;
	extern u64 rep_stack[1024];
	extern i32 game_ply;
	extern HistoryTable history;
	extern Move killers[MAX_PLY][NUM_KILLERS];
	
	void init();
	void clear_tables();
	
	// Optimized version that accepts pre-computed hash
	bool is_repetition(u64 hash, i32 ply);
	
	// Convenience wrapper for backward compatibility
	inline bool is_repetition(const Position& pos, i32 ply = 0) {
		return is_repetition(Zobrist::hash(pos), ply);
	}
	
	Move search(Position& pos, SearchInfo& info, i32 max_depth);
	void stop();
}

#endif // SEARCH_H
