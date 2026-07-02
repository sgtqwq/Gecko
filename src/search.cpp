#include "search.h"
#include "movegen.h"
#include "eval.h"
#include "tt.h"

#include <iostream>

namespace Search {
	
	std::atomic<bool> stopped{false};
	u64 rep_stack[1024]{};
	i32 game_ply = 0;
	
	void init() {
		stopped.store(false, std::memory_order_relaxed);
	}
	
	void clear_tables() {
	}
	
	bool is_repetition(const Position& pos, i32 ply) {
		const u64 hash = Zobrist::hash(pos);
		i32 count = 0;
		for (i32 i = game_ply + ply - 2; i >= 0; i -= 2) {
			if (rep_stack[i] == hash && ++count >= 2) {
				return true;
			}
		}
		return false;
	}
	
	static inline bool in_check(const Position& pos) {
		const i32 king_sq = BB::lsb(pos.colour[0] & pos.pieces[King]);
		return pos.is_attacked(king_sq, true);
	}
	
	static i32 quiescence(Position& pos, SearchInfo& info, i32 ply, i32 alpha, i32 beta) {
		info.nodes++;
		if ((info.nodes & 2047) == 0 && !info.infinite &&
			info.elapsed_time() >= info.time_limit) {
			stopped.store(true, std::memory_order_relaxed);
		}
		if (stopped.load(std::memory_order_relaxed)) return 0;
		
		if (ply >= MAX_PLY) return Eval::evaluate(pos);
		
		const bool check = in_check(pos);
		
		i32 stand_pat = 0;
		if (!check) {
			stand_pat = Eval::evaluate(pos);
			if (stand_pat >= beta) return stand_pat;
			if (stand_pat > alpha) alpha = stand_pat;
		}
		
		Move moves[MAX_MOVES];
		const i32 count = generate_moves(pos, moves, !check);
		
		i32 legal = 0;
		i32 best  = check ? (-MATE_SCORE + ply) : stand_pat;
		
		for (i32 i = 0; i < count; ++i) {
			if (stopped.load(std::memory_order_relaxed)) break;
			
			Position next = pos;
			if (!next.make_move(moves[i])) continue;
			legal++;
			
			const i32 score = -quiescence(next, info, ply + 1, -beta, -alpha);
			
			if (score > best) {
				best = score;
				if (score > alpha) {
					alpha = score;
					if (alpha >= beta) break;
				}
			}
		}
		
		return best;
	}
	
	static i32 negamax(Position& pos, SearchInfo& info, i32 depth, i32 ply, i32 alpha, i32 beta) {
		if (ply > 0 && is_repetition(pos, ply)) return 0;
		
		if (depth <= 0) return quiescence(pos, info, ply, alpha, beta);
		
		rep_stack[game_ply + ply] = Zobrist::hash(pos);
		
		Move moves[MAX_MOVES];
		const i32 count = generate_moves(pos, moves, false);
		i32 legal = 0, best = -INF;
		
		for (i32 i = 0; i < count && !stopped.load(std::memory_order_relaxed); ++i) {
			Position next = pos;
			if (!next.make_move(moves[i])) continue;
			legal++;
			info.nodes++;
			
			if ((info.nodes & 2047) == 0 && !info.infinite &&
				info.elapsed_time() >= info.time_limit) {
				stopped.store(true, std::memory_order_relaxed);
			}
			
			const i32 score = -negamax(next, info, depth - 1, ply + 1, -beta, -alpha);
			if (score > best) best = score;
			if (best > alpha) alpha = best;
			if (alpha >= beta) break;
		}
		
		if (legal == 0) {
			const i32 king_sq = BB::lsb(pos.colour[0] & pos.pieces[King]);
			return pos.is_attacked(king_sq) ? -MATE_SCORE + ply : 0;
		}
		
		return best;
	}
	
	Move search(Position& pos, SearchInfo& info, i32 max_depth) {
		stopped.store(false, std::memory_order_relaxed);
		info.reset();
		info.start_time = std::chrono::steady_clock::now();
		
		rep_stack[game_ply] = Zobrist::hash(pos);
		
		Move best       = NullMove;
		i32  best_score = -INF;
		
		for (i32 depth = 1; depth <= max_depth; ++depth) {
			Move moves[MAX_MOVES];
			const i32 count = generate_moves(pos, moves, false);
			i32 alpha = -INF, beta = INF;
			Move cur_best  = NullMove;
			i32  cur_score = -INF;
			
			for (i32 i = 0; i < count && !stopped.load(std::memory_order_relaxed); ++i) {
				Position next = pos;
				if (!next.make_move(moves[i])) continue;
				info.nodes++;
				
				const i32 score = -negamax(next, info, depth - 1, 1, -beta, -alpha);
				if (stopped.load(std::memory_order_relaxed)) break;
				
				if (score > cur_score) {
					cur_score = score;
					cur_best  = moves[i];
				}
				if (score > alpha) alpha = score;
			}
			
			if (stopped.load(std::memory_order_relaxed)) break;
			
			if (!cur_best.is_none()) {
				best       = cur_best;
				best_score = cur_score;
				info.depth = depth;
				
				const i64 elapsed = info.elapsed_time();
				const u64 nps     = elapsed > 0
				? (info.nodes * 1000ULL / static_cast<u64>(elapsed))
				: 0;
				
				std::cout << "info depth " << depth
				<< " score cp " << best_score
				<< " nodes "    << info.nodes
				<< " nps "      << nps
				<< " time "     << elapsed
				<< " pv "       << move_to_string(best, pos.flipped)
				<< std::endl;
			}
			
			if (!info.infinite && info.elapsed_time() >= info.time_limit) break;
		}
		
		return best;
	}
	
	void stop() {
		stopped.store(true, std::memory_order_relaxed);
	}
	
} // namespace Search

