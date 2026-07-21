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

constexpr i32 HISTORY_MAX = 16384;
constexpr i32 HISTORY_BONUS_MAX = 2000;
constexpr i32 CAPTURE_HISTORY_MAX = 16384;

struct HistoryTable {
	i32 quiet_history[2][64][64];
	
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
	
	i32 get_score(bool color, i32 from, i32 to) const {
		return quiet_history[color ? 1 : 0][from][to];
	}
	
	void update(bool color, i32 from, i32 to, i32 bonus) {
		i32& score = quiet_history[color ? 1 : 0][from][to];
		score += bonus - score * abs(bonus) / HISTORY_MAX;
		score = std::clamp(score, -HISTORY_MAX, HISTORY_MAX);
	}
};

struct CaptureHistoryTable {
	i32 history[2][6][64][6];
	
	CaptureHistoryTable() {
		clear();
	}
	
	void clear() {
		for (int c = 0; c < 2; ++c)
			for (int pt = 0; pt < 6; ++pt)
				for (int to = 0; to < 64; ++to)
					for (int vt = 0; vt < 6; ++vt)
						history[c][pt][to][vt] = 0;
	}
	
	i32 get_score(bool color, PieceType attacker, i32 to, PieceType victim) const {
		return history[color ? 1 : 0][attacker][to][victim];
	}
	
	void update(bool color, PieceType attacker, i32 to, PieceType victim, i32 bonus) {
		i32& score = history[color ? 1 : 0][attacker][to][victim];
		score += bonus - score * abs(bonus) / CAPTURE_HISTORY_MAX;
		score = std::clamp(score, -CAPTURE_HISTORY_MAX, CAPTURE_HISTORY_MAX);
	}
};

struct KillerTable {
	Move killers[MAX_PLY][2];
	
	KillerTable() {
		clear();
	}
	
	void clear() {
		for (i32 i = 0; i < MAX_PLY; ++i) {
			killers[i][0] = NullMove;
			killers[i][1] = NullMove;
		}
	}
	
	void update(i32 ply, Move move) {
		if (ply < 0 || ply >= MAX_PLY) return;
		if (killers[ply][0] == move) return;
		killers[ply][1] = killers[ply][0];
		killers[ply][0] = move;
	}
	
	bool is_killer(i32 ply, const Move& move) const {
		if (ply < 0 || ply >= MAX_PLY) return false;
		return killers[ply][0] == move || killers[ply][1] == move;
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
	extern KillerTable killers;
	extern CaptureHistoryTable capture_history;
	extern i32 static_eval_stack[MAX_PLY];
	
	void init();
	void clear_tables();
	
	bool is_repetition(u64 hash, i32 ply);
	
	inline bool is_repetition(const Position& pos, i32 ply = 0) {
		return is_repetition(Zobrist::hash(pos), ply);
	}
	
	Move search(Position& pos, SearchInfo& info, i32 max_depth);
	void stop();
}

#endif // SEARCH_H
