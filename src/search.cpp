#include "search.h"
#include "movegen.h"
#include "eval.h"
#include "tt.h"

#include <iostream>

namespace Search {
	
	std::atomic<bool> stopped{false};
	u64 rep_stack[1024]{};
	i32 game_ply = 0;
	
	// PNBRQKX (X = no piece)
	constexpr i32 MVV_LVA[7][7] = {
		{15, 14, 13, 12, 11, 10, 0}, // Taking a pawn
		{25, 24, 23, 22, 21, 20, 0}, // Taking a knight
		{35, 34, 33, 32, 31, 30, 0}, // Taking a bishop
		{45, 44, 43, 42, 41, 40, 0}, // Taking a rook
		{55, 54, 53, 52, 51, 50, 0}, // Taking a queen
		{0, 0, 0, 0, 0, 0, 0},       // Taking a king (should never happen)
		{0, 0, 0, 0, 0, 0, 0}        // No piece
	};
	
	static inline i32 mvv_lva_score(const Position& pos, const Move& move) {
		PieceType attacker = pos.piece_on(move.from);
		PieceType victim   = pos.piece_on(move.to);
		
		// Handle en passant: the "to" square is empty, but it's still a capture.
		if (victim == None && attacker == Pawn && pos.ep && BB::square_bb(move.to) == pos.ep) {
			victim = Pawn;
		}
		
		return MVV_LVA[victim][attacker];
	}
	
	static inline i32 score_to_tt(i32 score, i32 ply) {
		if (score > MATE_SCORE - MAX_PLY) return score + ply;
		if (score < -MATE_SCORE + MAX_PLY) return score - ply;
		return score;
	}
	
	static inline i32 score_from_tt(i32 score, i32 ply) {
		if (score > MATE_SCORE - MAX_PLY) return score - ply;
		if (score < -MATE_SCORE + MAX_PLY) return score + ply;
		return score;
	}
	
	static void order_moves(const Position& pos, Move* moves, i32 count, Move tt_move = NullMove) {
		i32 scores[MAX_MOVES];
		for (i32 i = 0; i < count; ++i) {
			scores[i] = (moves[i] == tt_move) ? 1000000 : mvv_lva_score(pos, moves[i]);
		}
		
		// Insertion sort (descending). Move lists are short, so this is
		// faster and simpler than std::sort here.
		for (i32 i = 1; i < count; ++i) {
			const Move m = moves[i];
			const i32  s = scores[i];
			i32 j = i - 1;
			while (j >= 0 && scores[j] < s) {
				moves[j + 1]  = moves[j];
				scores[j + 1] = scores[j];
				--j;
			}
			moves[j + 1]  = m;
			scores[j + 1] = s;
		}
	}
	
	void init() {
		stopped.store(false, std::memory_order_relaxed);
	}
	
	void clear_tables() {
	}
	
	// Optimized: accepts hash directly instead of recalculating
	bool is_repetition(u64 hash, i32 ply) {
		i32 count = 0;
		for (i32 i = game_ply + ply - 2; i >= 0; i -= 2) {
			if (rep_stack[i] == hash && ++count >= 1) {
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
		
		i32 stand_pat = Eval::evaluate(pos);
		if (stand_pat >= beta) return stand_pat;
		if (stand_pat > alpha) alpha = stand_pat;
		
		Move moves[MAX_MOVES];
		const i32 count = generate_moves(pos, moves, true);
		order_moves(pos, moves, count);
		
		i32 legal = 0;
		i32 best = stand_pat;
		
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
		// Calculate hash once and reuse it throughout
		const u64 key = Zobrist::hash(pos);
		
		// Check repetition using pre-computed hash
		if (ply > 0 && is_repetition(key, ply)) return 0;
		
		if (depth <= 0) return quiescence(pos, info, ply, alpha, beta);
		
		const i32 alpha_orig = alpha;
		Move tt_move = NullMove;
		
		// TT probe reuses the same hash
		if (TTEntry* entry = tt.probe(key)) {
			tt_move = entry->best_move;
			if (entry->depth >= depth) {
				const i32 tt_score = score_from_tt(entry->score, ply);
				if (entry->flag == TT_EXACT) return tt_score;
				if (entry->flag == TT_ALPHA && tt_score <= alpha) return tt_score;
				if (entry->flag == TT_BETA  && tt_score >= beta)  return tt_score;
			}
		}
		
		// Store hash in repetition stack (already computed)
		rep_stack[game_ply + ply] = key;
		
		Move moves[MAX_MOVES];
		const i32 count = generate_moves(pos, moves, false);
		order_moves(pos, moves, count, tt_move);
		
		i32 legal = 0, best = -INF;
		Move best_move = NullMove;
		
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
			if (score > best) {
				best = score;
				best_move = moves[i];
			}
			if (best > alpha) alpha = best;
			if (alpha >= beta) break;
		}
		
		if (legal == 0) {
			const i32 king_sq = BB::lsb(pos.colour[0] & pos.pieces[King]);
			return pos.is_attacked(king_sq) ? -MATE_SCORE + ply : 0;
		}
		
		if (!stopped.load(std::memory_order_relaxed)) {
			u8 flag = TT_EXACT;
			if (best <= alpha_orig) flag = TT_ALPHA;
			else if (best >= beta) flag = TT_BETA;
			tt.store(key, depth, score_to_tt(best, ply), flag, best_move);
		}
		
		return best;
	}
	
	Move search(Position& pos, SearchInfo& info, i32 max_depth) {
		stopped.store(false, std::memory_order_relaxed);
		info.reset();
		info.start_time = std::chrono::steady_clock::now();
		
		// Calculate hash once at root
		const u64 root_hash = Zobrist::hash(pos);
		rep_stack[game_ply] = root_hash;
		
		Move best       = NullMove;
		i32  best_score = -INF;
		
		for (i32 depth = 1; depth <= max_depth; ++depth) {
			Move moves[MAX_MOVES];
			const i32 count = generate_moves(pos, moves, false);
			order_moves(pos, moves, count);
			
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
