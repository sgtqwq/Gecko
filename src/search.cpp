#include "search.h"
#include "movegen.h"
#include "eval.h"
#include "tt.h"

#include <iostream>
#include <algorithm>
#include <cmath>

namespace Search {
	
	std::atomic<bool> stopped{false};
	u64 rep_stack[1024]{};
	i32 game_ply = 0;
	HistoryTable history;
	
	constexpr i32 LMR_MOVES = 250;
	constexpr i32 LMR_SCALE = 1024;
	u16 reduction[LMR_MOVES][MAX_PLY + 1]{};
	
	// RFP constants
	constexpr i32 RFP_DEPTH = 8;
	constexpr i32 RFP_MARGIN = 88;
	
	static void init_lmr() {
		for (i32 i = 1; i < LMR_MOVES; ++i) {
			for (i32 d = 1; d <= MAX_PLY; ++d) {
				const double r = (1.0 + std::log(i) * std::log(d) *0.42 ) * 1024;
				reduction[i][d] = static_cast<u16>(r);
			}
		}
	}
	
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
	
	static inline bool is_capture(const Position& pos, const Move& move) {
		// Normal capture
		if (pos.piece_on(move.to) != None) return true;
		// En passant
		if (pos.piece_on(move.from) == Pawn && pos.ep && BB::square_bb(move.to) == pos.ep) return true;
		return false;
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
	
	// Calculate history bonus based on depth
	static inline i32 history_bonus(i32 depth) {
		return std::min(HISTORY_BONUS_MAX, depth * depth + depth * 2);
	}
	
	static inline bool in_check(const Position& pos) {
		const i32 king_sq = BB::lsb(pos.colour[0] & pos.pieces[King]);
		return pos.is_attacked(king_sq, true);
	}
	
	static void order_moves(const Position& pos, Move* moves, i32 count, Move tt_move = NullMove, bool stm_flipped = false) {
		i32 scores[MAX_MOVES];
		
		for (i32 i = 0; i < count; ++i) {
			if (moves[i] == tt_move) {
				scores[i] = 1000000;  // TT move gets highest priority
			}
			else if (is_capture(pos, moves[i])) {
				// Captures scored by MVV-LVA (range: ~10-55)
				scores[i] = 100000 + mvv_lva_score(pos, moves[i]);
			}
			else {
				// Quiet moves scored by history heuristic
				scores[i] = history.get_score(stm_flipped, moves[i].from, moves[i].to);
			}
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
		history.clear();
		init_lmr();
	}
	
	void clear_tables() {
		history.clear();
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
		order_moves(pos, moves, count, NullMove, pos.flipped);
		
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
	
	static i32 negamax(Position& pos, SearchInfo& info, i32 depth, i32 ply, i32 alpha, i32 beta, bool pv_node) {
		const bool root = (ply == 0);
		info.seldepth = std::max(info.seldepth, ply);
		
		// Calculate hash once and reuse it throughout
		const u64 key = Zobrist::hash(pos);
		
		// Check repetition using pre-computed hash. The root position itself should not draw.
		if (!root && is_repetition(key, ply)) return 0;
		
		// Check if we're in check - needed for extensions
		const bool in_check_now = in_check(pos);
		
		// Check extension: extend search by 1 ply when in check
		if (in_check_now && depth < MAX_PLY - 1) {
			depth++;
		}
		
		if (depth <= 0) return quiescence(pos, info, ply, alpha, beta);
		
		const i32 alpha_orig = alpha;
		Move tt_move = NullMove;
		
		// TT probe reuses the same hash. Do not cut off at root; root must refresh bestmove.
		if (TTEntry* entry = tt.probe(key)) {
			tt_move = entry->best_move;
			if (!root && entry->depth >= depth) {
				const i32 tt_score = score_from_tt(entry->score, ply);
				if (entry->flag == TT_EXACT) return tt_score;
				if (entry->flag == TT_ALPHA && tt_score <= alpha) return tt_score;
				if (entry->flag == TT_BETA  && tt_score >= beta)  return tt_score;
			}
		}
		
		// Reverse Futility Pruning (RFP)
		// Only apply in non-PV nodes, when not in check, and at shallow depths
		if (!pv_node && !in_check_now && depth <= RFP_DEPTH) {
			const i32 eval = Eval::evaluate(pos);
			const i32 rfp_margin = RFP_MARGIN * depth;
			
			// If our position is so good that even with a margin we're above beta, prune
			if (eval - rfp_margin >= beta) {
				return eval - rfp_margin;
			}
		}
		
		// Store hash in repetition stack (already computed)
		rep_stack[game_ply + ply] = key;
		
		Move moves[MAX_MOVES];
		const i32 count = generate_moves(pos, moves, false);
		order_moves(pos, moves, count, tt_move, pos.flipped);
		
		i32 legal = 0, best = -INF;
		Move best_move = NullMove;
		
		// Track quiet moves tried for history updates
		Move quiets_tried[MAX_MOVES];
		i32 quiets_count = 0;
		
		for (i32 i = 0; i < count && !stopped.load(std::memory_order_relaxed); ++i) {
			Position next = pos;
			if (!next.make_move(moves[i])) continue;
			
			const i32 move_index = legal;
			legal++;
			info.nodes++;
			
			if ((info.nodes & 2047) == 0 && !info.infinite &&
				info.elapsed_time() >= info.time_limit) {
				stopped.store(true, std::memory_order_relaxed);
			}
			
			const bool is_quiet = !is_capture(pos, moves[i]) && moves[i].promo == None;
			const i32 new_depth = depth - 1;
			
			// A child is a PV node if we're at a PV node and it's the first move
			const bool child_pv = pv_node && (move_index == 0);
			
			i32 score;
			
			if (depth >= 2 && move_index >= 1 + 2 * root) {
				const i32 r_idx = std::min(move_index, LMR_MOVES - 1);
				i32 r = reduction[r_idx][std::min(depth, MAX_PLY)];
				if(!is_quiet) r /= 2;
				const i32 searched_depth = std::clamp(new_depth - r / LMR_SCALE, 1, new_depth);
				
				score = -negamax(next, info, searched_depth, ply + 1, -beta, -alpha, false);
				
				if (!stopped.load(std::memory_order_relaxed) && score > alpha && searched_depth < new_depth) {
					// Reduced search failed high, so verify at full depth.
					score = -negamax(next, info, new_depth, ply + 1, -beta, -alpha, child_pv);
				}
			}
			else {
				score = -negamax(next, info, new_depth, ply + 1, -beta, -alpha, child_pv);
			}
			
			if (stopped.load(std::memory_order_relaxed)) break;
			
			if (score > best) {
				best = score;
				best_move = moves[i];
				if (root) {
					info.pv[0] = best_move;
					info.pv_length = 1;
				}
			}
			
			if (best > alpha) {
				alpha = best;
				if (alpha >= beta) {
					// Beta cutoff - update history for the move that caused it
					if (is_quiet) {
						const i32 bonus = history_bonus(depth);
						history.update(pos.flipped, best_move.from, best_move.to, bonus);
						
						// Penalize other quiets that were tried before the cutoff
						for (i32 j = 0; j < quiets_count; ++j) {
							history.update(pos.flipped, quiets_tried[j].from, quiets_tried[j].to, -bonus);
						}
					}
					break;
				}
			}
			
			// Track quiet moves for later penalty if we get a cutoff
			if (is_quiet && quiets_count < MAX_MOVES) {
				quiets_tried[quiets_count++] = moves[i];
			}
		}
		
		if (legal == 0) {
			// Checkmate or stalemate
			return in_check_now ? -MATE_SCORE + ply : 0;
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
			info.pv[0] = best;
			info.pv_length = best.is_none() ? 0 : 1;
			
			// Root is always a PV node
			const i32 score = negamax(pos, info, depth, 0, -INF, INF, true);
			if (stopped.load(std::memory_order_relaxed)) break;
			
			if (!info.pv[0].is_none()) {
				best       = info.pv[0];
				best_score = score;
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
