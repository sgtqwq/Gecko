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
	
	// LMR reduction table (plies to reduce, already includes base offset).
	i32 reduction[256][MAX_PLY + 1]{};
	
	static void init_lmr() {
		for (i32 i = 1; i < 256; ++i) {
			for (i32 d = 1; d <= MAX_PLY; ++d) {
				reduction[i][d] = static_cast<i32>(1.148 + std::log(i) * std::log(d) / 2.43);
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
	
	// Late Move Pruning constants.
	// Mirrors the reference formula: (LMP_BASE + depth * depth) / (2 - improving).
	constexpr i32 LMP_BASE      = 3;
	constexpr i32 LMP_MAX_DEPTH = 8;
	
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
	
	static inline bool has_non_pawn_material(const Position& pos) {
		return (pos.colour[0] &
			(pos.pieces[Knight] | pos.pieces[Bishop] |
				pos.pieces[Rook]   | pos.pieces[Queen])) != 0;
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
		
		// Static eval, computed once and reused by the pruning heuristics
		// below. When in check the value is never actually used (every
		// consumer is guarded by !in_check_now), so a dummy value avoids
		// paying for a real evaluation in that case.
		const i32 eval = in_check_now ? 0 : Eval::evaluate(pos);
		
		// Record the static eval for this ply (or "none" when in check) so
		// that the improving heuristic below can look 2 plies back, at the
		// last time it was our turn to move.
		if (ply < MAX_PLY) {
			info.static_eval[ply] = in_check_now ? EVAL_NONE : eval;
		}
		
		// Improving: true if our static eval got better compared to the
		// last time we were on move (2 plies ago). Used to scale down
		// pruning when our position seems to be getting worse, and prune
		// more aggressively when it's getting better.
		bool improving = false;
		if (!in_check_now && ply >= 2 && info.static_eval[ply - 2] != EVAL_NONE) {
			improving = eval > info.static_eval[ply - 2];
		}
		
		// Reverse Futility Pruning: only in non-PV, not in check, shallow depth.
		if (!pv_node && !in_check_now && depth <= 8) {
			const i32 rfp_margin = 88 * depth;
			
			if (eval - rfp_margin >= beta) {
				return eval - rfp_margin;
			}
		}
		
		// Null Move Pruning: only in non-PV, not in check, sufficient depth,
		// and not in a (near-)pure pawn ending where zugzwang is likely.
		if (!pv_node && !in_check_now && depth >= 3 && has_non_pawn_material(pos)) {
			if (eval >= beta + 25) {
				const i32 R = 4 + depth / 3;
				
				Position null_pos = pos;
				null_pos.make_null_move();
				
				const i32 null_score = -negamax(null_pos, info, depth - R, ply + 1,
					-beta, -beta + 1, false);
				
				if (stopped.load(std::memory_order_relaxed)) return 0;
				
				if (null_score >= beta) {
					// Do not trust mate scores returned by a null-move search;
					// they are not verified and can be "fake" mates.
					if (null_score >= MATE_SCORE - MAX_PLY) {
						return beta;
					}
					return null_score;
				}
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
		i32 lmp_count = (LMP_BASE + depth * depth) / (2 - (improving ? 1 : 0));
		for (i32 i = 0; i < count && !stopped.load(std::memory_order_relaxed); ++i) {
			
			// Late Move Pruning.
			const bool is_quiet = !is_capture(pos, moves[i]) && moves[i].promo == None;
			if (!root && is_quiet) {
				if (legal >= lmp_count) {
					break;
				}
			}
			
			Position next = pos;
			if (!next.make_move(moves[i])) continue;
			
			const i32 move_index = legal;
			legal++;
			info.nodes++;
			
			if ((info.nodes & 2047) == 0 && !info.infinite &&
				info.elapsed_time() >= info.time_limit) {
				stopped.store(true, std::memory_order_relaxed);
			}
			
			const i32 new_depth = depth - 1;
			
			// A child is a PV node if we're at a PV node and it's the first move
			const bool child_pv = pv_node && (move_index == 0);
			
			i32 score;
			
			if (depth >= 2 && move_index >= 1 + 2 * root) {
				const i32 r_idx = std::min(move_index, 255);
				const i32 r = reduction[r_idx][std::min(depth, MAX_PLY)];
				const i32 searched_depth = std::clamp(new_depth - r, 1, new_depth);
				
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
	
	static void report_uci_info(SearchInfo& info, const Position& pos, Move best, i32 score, i32 depth, bool is_lowerbound = false, bool is_upperbound = false) {
		const i64 elapsed = info.elapsed_time();
		const u64 nps     = elapsed > 0
		? (info.nodes * 1000ULL / static_cast<u64>(elapsed))
		: 0;
		
		std::cout << "info depth " << depth
		<< " score cp " << score;
		
		// Add bound type if applicable
		if (is_lowerbound) {
			std::cout << " lowerbound";
		} else if (is_upperbound) {
			std::cout << " upperbound";
		}
		
		std::cout << " nodes "    << info.nodes
		<< " nps "      << nps
		<< " time "     << elapsed
		<< " pv "       << move_to_string(best, pos.flipped)
		<< std::endl;
	}
	
	Move search(Position& pos, SearchInfo& info, i32 max_depth) {
		stopped.store(false, std::memory_order_relaxed);
		info.reset();
		info.start_time = std::chrono::steady_clock::now();
		
		// Calculate hash once at root
		const u64 root_hash = Zobrist::hash(pos);
		rep_stack[game_ply] = root_hash;
		
		Move best       = NullMove;
		i32  best_score = 0;
		
		for (i32 depth = 1; depth <= max_depth; ++depth) {
			info.pv[0] = best;
			info.pv_length = best.is_none() ? 0 : 1;
			
			// Aspiration window search around the previous score.
			i32 delta     = 25;
			i32 alpha     = -INF;
			i32 beta      = INF;
			i32 asp_depth = depth;
			
			if (depth >= 4) {
				alpha = std::max(best_score - delta, -INF);
				beta  = std::min(best_score + delta, INF);
			}
			
			i32 score;
			while (true) {
				score = negamax(pos, info, std::max(asp_depth, 1), 0, alpha, beta, true);
				
				if (stopped.load(std::memory_order_relaxed)) break;
				
				if ((score <= alpha || score >= beta) && info.elapsed_time() > 1000) {
					// Report with bound information when window fails
					if (score <= alpha) {
						report_uci_info(info, pos, info.pv[0], alpha, depth, false, true); // upperbound
					} else {
						report_uci_info(info, pos, info.pv[0], beta, depth, true, false);  // lowerbound
					}
				}
				
				if (score <= alpha) {
					beta      = (alpha + beta) / 2;
					alpha     = std::max(alpha - delta, -INF);
					asp_depth = depth;
				}
				else if (score >= beta) {
					beta      = std::min(beta + delta, INF);
					//From Sirius
					asp_depth = std::max(asp_depth - 1, depth - 5);
				}
				else {
					break;
				}
				
				delta += delta * 0.2;
			}
			
			if (stopped.load(std::memory_order_relaxed)) break;
			
			best_score = score;
			
			if (!info.pv[0].is_none()) {
				best       = info.pv[0];
				info.depth = depth;
				report_uci_info(info, pos, best, best_score, depth);
			}
			
			if (!info.infinite && info.elapsed_time() >= info.time_limit) break;
		}
		
		return best;
	}
	
	void stop() {
		stopped.store(true, std::memory_order_relaxed);
	}
	
} // namespace Search
