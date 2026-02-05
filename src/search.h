#ifndef SEARCH_H
#define SEARCH_H

#include "types.h"
#include "position.h"
#include <atomic>
#include <chrono>

constexpr i32 INF = 30000;
constexpr i32 MATE_SCORE = 29000;
constexpr i32 MAX_PLY = 64;
constexpr i32 MAX_MOVES = 256;
constexpr i32 MAX_HISTORY = 16384;

struct SearchInfo {
	u64 nodes;
	i32 depth;
	i32 seldepth;
	Move pv[MAX_PLY];
	i32 pv_length;
	
	std::chrono::steady_clock::time_point start_time;
	i64 time_limit;
	bool infinite;
	
	SearchInfo() : nodes(0), depth(0), seldepth(0), pv_length(0), 
	time_limit(0), infinite(true) {}
	
	void reset() {
		nodes = 0;
		depth = 0;
		seldepth = 0;
		pv_length = 0;
	}
};

namespace Search {
	extern std::atomic<bool> stopped;
	
	extern u64 rep_stack[256];
	extern i32 game_ply;
	
	extern i32 history[64][64];
	extern Move killers[MAX_PLY][2];
	
	extern i32 lmr_table[MAX_PLY][MAX_MOVES];
	
	void init();
	void clear_tables();
	Move search(Position& pos, SearchInfo& info, i32 max_depth);
	void stop();
}

#endif // SEARCH_H
