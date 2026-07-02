#ifndef SEARCH_H
#define SEARCH_H

#include "types.h"
#include "position.h"

#include <atomic>
#include <chrono>

constexpr i32 INF = 30000;
constexpr i32 MATE_SCORE = 29000;
constexpr i32 MAX_PLY = 256;
constexpr i32 MAX_MOVES = 256;

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

	void init();
	void clear_tables();
	bool is_repetition(const Position& pos, i32 ply = 0);
	Move search(Position& pos, SearchInfo& info, i32 max_depth);
	void stop();
}

#endif // SEARCH_H
