#include "search.h"
#include "movegen.h"
#include "eval.h"
#include "tt.h"
#include "see.h"

#include <iostream>
#include <algorithm>
#include <cmath>

namespace Search {
	
	std::atomic<bool> stopped{false};
	u64 rep_stack[1024]{};
	i32 game_ply = 0;
	HistoryTable history;
	KillerTable killers;
	CaptureHistoryTable capture_history;
	i32 eval_stack[MAX_PLY + 4];
	
	i32 reduction[256][MAX_PLY + 1]{};
	
	static void init_lmr() {
		for (i32 i = 1; i < 256; ++i) {
			for (i32 d = 1; d <= MAX_PLY; ++d) {
				reduction[i][d] = static_cast<i32>(1.148 + std::log(i) * std::log(d) / 2.43);
			}
		}
	}
	
	constexpr i32 MVV_LVA[7][7] = {
		{15, 14, 13, 12, 11, 10, 0},
		{25, 24, 23, 22, 21, 20, 0},
		{35, 34, 33, 32, 31, 30, 0},
		{45, 44, 43, 42, 41, 40, 0},
		{55, 54, 53, 52, 51, 50, 0},
		{0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0}
	};
	
	static inline PieceType captured_piece_type(const Position& pos, const Move& move) {
		PieceType victim = pos.piece_on(move.to);
		if (victim == None && pos.piece_on(move.from) == Pawn &&
			pos.ep && BB::square_bb(move.to) == pos.ep) {
			victim = Pawn;
		}
		return victim;
	}
	
	static inline i32 mvv_lva_score(const Position& pos, const Move& move) {
		PieceType attacker = pos.piece_on(move.from);
		PieceType victim   = captured_piece_type(pos, move);
		return MVV_LVA[victim][attacker];
	}
	
	static inline bool is_capture(const Position& pos, const Move& move) {
		if (pos.piece_on(move.to) != None) return true;
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
	
	static inline bool compute_improving(i32 ply, bool in_check_now, i32 eval) {
		if (in_check_now) return false;
		
		if (ply >= 2 && eval_stack[ply - 2] != VALUE_NONE)
			return eval > eval_stack[ply - 2];
		
		if (ply >= 4 && eval_stack[ply - 4] != VALUE_NONE)
			return eval > eval_stack[ply - 4];
		
		return true;
	}
	
	static void order_moves(const Position& pos, Move* moves, i32 count, i32 ply,
		Move tt_move = NullMove, bool stm_flipped = false) {
			i32 scores[MAX_MOVES];
			
			const Move killer1 = (ply >= 0 && ply < MAX_PLY) ? killers.killers[ply][0] : NullMove;
			const Move killer2 = (ply >= 0 && ply < MAX_PLY) ? killers.killers[ply][1] : NullMove;
			
			for (i32 i = 0; i < count; ++i) {
				if (moves[i] == tt_move) {
					scores[i] = 1000000;
				}
				else if (is_capture(pos, moves[i])) {
					const PieceType attacker = pos.piece_on(moves[i].from);
					const PieceType victim   = captured_piece_type(pos, moves[i]);
					scores[i] = 100000 + mvv_lva_score(pos, moves[i]) * 100
					+ capture_history.get_score(stm_flipped, attacker, moves[i].to, victim) / 32;
				}
				else if (moves[i] == killer1) {
					scores[i] = 90000;
				}
				else if (moves[i] == killer2) {
					scores[i] = 89000;
				}
				else {
					scores[i] = history.get_score(stm_flipped, moves[i].from, moves[i].to);
				}
			}
			
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
		killers.clear();
		capture_history.clear();
		std::fill(std::begin(eval_stack), std::end(eval_stack), VALUE_NONE);
		init_lmr();
	}
	
	void clear_tables() {
		history.clear();
		killers.clear();
		capture_history.clear();
	}
	
	bool is_repetition(u64 hash, i32 ply) {
		i32 count = 0;
		for (i32 i = game_ply + ply - 2; i >= 0; i -= 2) {
			if (rep_stack[i] == hash && ++count >= 1) {
				return true;
			}
		}
		return false;
	}
	
	static inline bool tt_score_can_correct_eval(const TTEntry& entry, i32 tt_score, i32 eval) {
		if (std::abs(tt_score) >= MATE_SCORE - MAX_PLY) return false;
		if (entry.flag == TT_EXACT) return true;
		if (tt_score > eval) return entry.flag != TT_ALPHA;
		return entry.flag != TT_BETA;
	}
	
	static i32 quiescence(Position& pos, SearchInfo& info, i32 ply, i32 alpha, i32 beta, u64 key) {
		info.nodes++;
		if ((info.nodes & 2047) == 0 && !info.infinite &&
			info.elapsed_time() >= info.time_limit) {
			stopped.store(true, std::memory_order_relaxed);
		}
		if (stopped.load(std::memory_order_relaxed)) return 0;
		
		const bool pv_node = (beta - alpha) > 1;
		
		if (ply >= MAX_PLY) return Eval::evaluate(pos);
		
		TTEntry* entry = tt.probe(key);
		Move tt_move = NullMove;
		i32 tt_score = TT_NO_SCORE;
		
		if (entry) {
			tt_move = entry->best_move();
			if (entry->has_score()) {
				tt_score = score_from_tt(entry->score, ply);
				if (!pv_node) {
					if (entry->flag == TT_EXACT) return tt_score;
					if (entry->flag == TT_ALPHA && tt_score <= alpha) return tt_score;
					if (entry->flag == TT_BETA  && tt_score >= beta)  return tt_score;
				}
			}
		}
		
		const i32 raw_eval = entry && entry->has_static_eval()
		? entry->static_eval
		: Eval::evaluate(pos);
		
		if (!entry || !entry->has_static_eval())
			tt.store_eval(key, raw_eval);
		
		i32 stand_pat = raw_eval;
		if (entry && entry->has_score() && tt_score_can_correct_eval(*entry, tt_score, stand_pat))
			stand_pat = tt_score;
		
		if (stand_pat >= beta) return stand_pat;
		if (stand_pat > alpha) alpha = stand_pat;
		
		Move moves[MAX_MOVES];
		const i32 count = generate_moves(pos, moves, true);
		order_moves(pos, moves, count, ply, tt_move, pos.flipped);
		
		i32 best = stand_pat;
		Move best_move = NullMove;
		u8 flag = TT_ALPHA;
		
		for (i32 i = 0; i < count; ++i) {
			if (stopped.load(std::memory_order_relaxed)) break;
			
			if (!SEE::ge(pos, moves[i], 0)) {
				continue;
			}
			
			Position next = pos;
			if (!next.make_move(moves[i])) continue;
			
			const u64 child_key = Zobrist::hash(next);
			tt.prefetch(child_key);
			const i32 score = -quiescence(next, info, ply + 1, -beta, -alpha, child_key);
			
			if (score > best) {
				best = score;
				best_move = moves[i];
				if (score > alpha) {
					alpha = score;
					flag = TT_EXACT;
					if (alpha >= beta) {
						flag = TT_BETA;
						break;
					}
				}
			}
		}
		
		if (!stopped.load(std::memory_order_relaxed)) {
			const Move stored_move = !best_move.is_none() ? best_move : tt_move;
			tt.store(key, 0, score_to_tt(best, ply), raw_eval, flag, stored_move);
		}
		
		return best;
	}
	
	static i32 negamax(Position& pos, SearchInfo& info, i32 depth, i32 ply,
		i32 alpha, i32 beta, bool cut_node, u64 key) {
			const bool root    = (ply == 0);
			const bool pv_node = (beta - alpha) > 1;
			
			info.seldepth = std::max(info.seldepth, ply);
			
			if (!root && is_repetition(key, ply)) return 0;
			rep_stack[game_ply + ply] = key;
			const bool in_check_now = in_check(pos);
			
			if (in_check_now && depth < MAX_PLY - 1) {
				depth++;
			}
			
			if (depth <= 0) return quiescence(pos, info, ply, alpha, beta, key);
			
			const i32 alpha_orig = alpha;
			Move tt_move = NullMove;
			TTEntry* tt_entry = tt.probe(key);
			i32 tt_score = TT_NO_SCORE;
			
			if (tt_entry) {
				tt_move = tt_entry->best_move();
				if (tt_entry->has_score()) {
					tt_score = score_from_tt(tt_entry->score, ply);
					if (!pv_node && tt_entry->depth >= depth) {
						if (tt_entry->flag == TT_EXACT) return tt_score;
						if (tt_entry->flag == TT_ALPHA && tt_score <= alpha) return tt_score;
						if (tt_entry->flag == TT_BETA  && tt_score >= beta)  return tt_score;
					}
				}
			}
			
			if (depth >= 3 && tt_move == NullMove) {
				depth--;
			}
			
			i32 raw_eval = TT_NO_SCORE;
			i32 eval = 0;
			if (!in_check_now) {
				raw_eval = tt_entry && tt_entry->has_static_eval()
				? tt_entry->static_eval
				: Eval::evaluate(pos);
				
				if (!tt_entry || !tt_entry->has_static_eval())
					tt.store_eval(key, raw_eval);
				
				eval = raw_eval;
				if (tt_entry && tt_entry->has_score()
					&& tt_score_can_correct_eval(*tt_entry, tt_score, eval)) {
					eval = tt_score;
				}
			}
			
			if (ply >= 0 && ply < MAX_PLY + 4)
				eval_stack[ply] = in_check_now ? VALUE_NONE : eval;
			const bool improving = compute_improving(ply, in_check_now, eval);
			(void)improving;
			
			if (!pv_node
				&& !in_check_now
				&& depth <= 7
				&& eval + 320 * depth < alpha) {
				const i32 razor_score = quiescence(pos, info, ply, alpha, beta, key);
				if (razor_score <= alpha) {
					return razor_score;
				}
			}
			
			if (!pv_node && !in_check_now && depth <= 8) {
				const i32 rfp_margin = 88 * depth;
				if (eval - rfp_margin >= beta) {
					return (eval + beta) / 2;
				}
			}
			
			if (!pv_node && !in_check_now && depth >= 3 && has_non_pawn_material(pos)) {
				if (eval >= beta + 25) {
					const i32 R = 4 + depth / 3;
					
					Position null_pos = pos;
					null_pos.make_null_move();
					const u64 null_key = Zobrist::hash(null_pos);
					tt.prefetch(null_key);
					
					const i32 null_score = -negamax(null_pos, info, depth - R, ply + 1,
						-beta, -beta + 1, !cut_node, null_key);
					
					if (stopped.load(std::memory_order_relaxed)) return 0;
					
					if (null_score >= beta) {
						if (null_score >= MATE_SCORE - MAX_PLY) {
							return beta;
						}
						return null_score;
					}
				}
			}
			
			Move moves[MAX_MOVES];
			const i32 count = generate_moves(pos, moves, false);
			order_moves(pos, moves, count, ply, tt_move, pos.flipped);
			
			i32 legal = 0, best = -INF;
			Move best_move = NullMove;
			
			Move quiets_tried[MAX_MOVES];
			i32 quiets_count = 0;
			
			Move captures_tried[MAX_MOVES];
			i32 captures_count = 0;
			
			const i32 lmp_threshold = (7 + depth * depth) / (2 - improving);
			const bool can_lmp = !pv_node && !in_check_now && depth <= 5;
			
			for (i32 i = 0; i < count && !stopped.load(std::memory_order_relaxed); ++i) {
				Position next = pos;
				if (!next.make_move(moves[i])) continue;
				
				const u64 child_key = Zobrist::hash(next);
				tt.prefetch(child_key);
				const i32 move_index = legal;
				legal++;
				info.nodes++;
				
				if ((info.nodes & 2047) == 0 && !info.infinite &&
					info.elapsed_time() >= info.time_limit) {
					stopped.store(true, std::memory_order_relaxed);
				}
				
				const bool is_cap   = is_capture(pos, moves[i]);
				const bool is_quiet = !is_cap && moves[i].promo == None;
				
				if (can_lmp && is_quiet && move_index >= lmp_threshold) {
					break;
				}
				
				const i32 new_depth = depth - 1;
				i32 score = -INF;
				
				if (depth >= 2 && move_index >= 1 + 2 * root) {
					const i32 r_idx = std::min(move_index, 255);
					i32 r = reduction[r_idx][std::min(depth, MAX_PLY)];
					if (!is_quiet) r = r * 3 / 5;
					const i32 lmr_depth = std::clamp(new_depth - r, 1, new_depth);
					
					score = -negamax(next, info, lmr_depth, ply + 1,
						-alpha - 1, -alpha, true, child_key);
					
					if (!stopped.load(std::memory_order_relaxed)
						&& score > alpha && lmr_depth < new_depth) {
						score = -negamax(next, info, new_depth, ply + 1,
							-alpha - 1, -alpha, !cut_node, child_key);
					}
				}
				else if (!pv_node || move_index > 0) {
					score = -negamax(next, info, new_depth, ply + 1,
						-alpha - 1, -alpha, !cut_node, child_key);
				}
				
				if (!stopped.load(std::memory_order_relaxed)
					&& pv_node && (move_index == 0 || score > alpha)) {
					score = -negamax(next, info, new_depth, ply + 1,
						-beta, -alpha, false, child_key);
				}
				
				if (stopped.load(std::memory_order_relaxed)) break;
				
				if (score > best) {
					best = score;
					
					if (score > alpha) {
						alpha = score;
						best_move = moves[i];
						
						if (root) {
							info.pv[0] = best_move;
							info.pv_length = 1;
						}
						
						if (alpha >= beta) {
							const i32 bonus = history_bonus(depth);
							
							if (is_quiet) {
								history.update(pos.flipped, best_move.from, best_move.to, bonus);
								killers.update(ply, best_move);
								
								for (i32 j = 0; j < quiets_count; ++j) {
									history.update(pos.flipped, quiets_tried[j].from, quiets_tried[j].to, -bonus);
								}
							}
							else if (is_cap) {
								const PieceType attacker = pos.piece_on(best_move.from);
								const PieceType victim   = captured_piece_type(pos, best_move);
								capture_history.update(pos.flipped, attacker, best_move.to, victim, bonus);
							}
							
							for (i32 j = 0; j < captures_count; ++j) {
								const PieceType attacker_j = pos.piece_on(captures_tried[j].from);
								const PieceType victim_j   = captured_piece_type(pos, captures_tried[j]);
								capture_history.update(pos.flipped, attacker_j, captures_tried[j].to, victim_j, -bonus);
							}
							
							break;
						}
					}
					else if (root && move_index == 0) {
						info.pv[0] = moves[i];
						info.pv_length = 1;
					}
				}
				
				if (is_quiet && quiets_count < MAX_MOVES) {
					quiets_tried[quiets_count++] = moves[i];
				}
				else if (is_cap && captures_count < MAX_MOVES) {
					captures_tried[captures_count++] = moves[i];
				}
			}
			
			if (legal == 0) {
				return in_check_now ? -MATE_SCORE + ply : 0;
			}
			
			if (!stopped.load(std::memory_order_relaxed)) {
				u8 flag = TT_EXACT;
				if (best <= alpha_orig) flag = TT_ALPHA;
				else if (best >= beta) flag = TT_BETA;
				const Move stored_move = !best_move.is_none() ? best_move : tt_move;
				tt.store(key, depth, score_to_tt(best, ply), raw_eval, flag, stored_move);
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
		
		const u64 root_hash = Zobrist::hash(pos);
		rep_stack[game_ply] = root_hash;
		
		Move best       = NullMove;
		i32  best_score = 0;
		
		for (i32 depth = 1; depth <= max_depth; ++depth) {
			info.pv[0] = best;
			info.pv_length = best.is_none() ? 0 : 1;
			
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
				score = negamax(pos, info, std::max(asp_depth, 1), 0, alpha, beta, false, root_hash);
				
				if (stopped.load(std::memory_order_relaxed)) break;
				
				if ((score <= alpha || score >= beta) && info.elapsed_time() > 1000) {
					if (score <= alpha) {
						report_uci_info(info, pos, info.pv[0], alpha, depth, false, true);
					} else {
						report_uci_info(info, pos, info.pv[0], beta, depth, true, false);
					}
				}
				
				if (score <= alpha) {
					beta      = (alpha + beta) / 2;
					alpha     = std::max(alpha - delta, -INF);
					asp_depth = depth;
				}
				else if (score >= beta) {
					beta      = std::min(beta + delta, INF);
					asp_depth = std::max(asp_depth - 1, depth - 5);
				}
				else {
					break;
				}
				
				delta += delta / 5;
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
