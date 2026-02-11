#include "search.h"
#include "movegen.h"
#include "eval.h"
#include "tt.h"
#include "bitboard.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace Search {
	
	std::atomic<bool> stopped{false};
	
	u64 rep_stack[1024];
	i32 game_ply = 0;
	
	i32 history[64][64];
	Move killers[MAX_PLY][2];
	i32 lmr_table[MAX_PLY][MAX_MOVES];
	
	i32 eval_stack[MAX_PLY + 4];
	
	constexpr i32 MAX_HISTORY = 2000;
	
	const i32 MVV_LVA[6][6] = {
		// attacker: P    N    B    R    Q    K
		/* P */    { 15,  14,  13,  12,  11,  10 },
		/* N */    { 25,  24,  23,  22,  21,  20 },
		/* B */    { 35,  34,  33,  32,  31,  30 },
		/* R */    { 45,  44,  43,  42,  41,  40 },
		/* Q */    { 55,  54,  53,  52,  51,  50 },
		/* K */    { 0,   0,   0,   0,   0,   0  },
	};
	
	void init_lmr_table() {
		for (i32 depth = 0; depth < MAX_PLY; depth++) {
			for (i32 moves = 0; moves < MAX_MOVES; moves++) {
				if (depth == 0 || moves == 0) {
					lmr_table[depth][moves] = 0;
				} else {
					lmr_table[depth][moves] = static_cast<i32>(
						0.5 + std::log(depth) * std::log(moves) * 0.5
						);
				}
			}
		}
	}
	
	inline Move flip_move(const Move& m) {
		return Move(m.from ^ 56, m.to ^ 56, m.promo);
	}
	
	inline void update_history(i32 from, i32 to, i32 bonus) {
		i32& h = history[from][to];
		bonus = std::clamp(bonus, -MAX_HISTORY, MAX_HISTORY);
		h += bonus - h * std::abs(bonus) / MAX_HISTORY;
	}
	
	inline void update_killers(i32 ply, const Move& move) {
		if (!(killers[ply][0] == move)) {
			killers[ply][1] = killers[ply][0];
			killers[ply][0] = move;
		}
	}
	
	void clear_tables() {
		std::memset(history, 0, sizeof(history));
		std::memset(killers, 0, sizeof(killers));
		std::memset(eval_stack, 0, sizeof(eval_stack));
	}
	
	i32 score_move(const Position& pos, const Move& move, const Move& tt_move, i32 ply) {
		if (move == tt_move) return 1000000;
		
		PieceType captured = pos.piece_on(move.to);
		
		if (captured != None) {
			PieceType attacker = pos.piece_on(move.from);
			return 100000 + MVV_LVA[captured][attacker];
		}
		
		if (move.promo != None) {
			return 95000 + move.promo;
		}
		
		if (move == killers[ply][0]) return 90000;
		if (move == killers[ply][1]) return 80000;
		
		return history[move.from][move.to];
	}
	
	void score_moves(const Position& pos, Move* moves, i32* scores, i32 count, const Move& tt_move, i32 ply) {
		for (i32 i = 0; i < count; i++) {
			scores[i] = score_move(pos, moves[i], tt_move, ply);
		}
	}
	
	void pick_move(Move* moves, i32* scores, i32 count, i32 current) {
		i32 best_idx = current;
		for (i32 i = current + 1; i < count; i++) {
			if (scores[i] > scores[best_idx]) {
				best_idx = i;
			}
		}
		if (best_idx != current) {
			std::swap(moves[current], moves[best_idx]);
			std::swap(scores[current], scores[best_idx]);
		}
	}
	
	bool check_time(SearchInfo& info) {
		if (stopped.load(std::memory_order_relaxed)) return true;
		
		if (!info.infinite && (info.nodes & 2047) == 0) {
			auto now = std::chrono::steady_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - info.start_time).count();
			if (elapsed >= info.time_limit) {
				stopped.store(true, std::memory_order_relaxed);
				return true;
			}
		}
		return false;
	}
	
	i32 quiescence(Position& pos, i32 alpha, i32 beta, i32 ply, SearchInfo& info) {
		if (check_time(info)) return 0;
		
		info.nodes++;
		if (ply > info.seldepth) info.seldepth = ply;
		
		i32 stand_pat = Eval::evaluate(pos);
		
		if (stand_pat >= beta) return beta;
		if (stand_pat > alpha) alpha = stand_pat;
		
		Move moves[MAX_MOVES];
		i32 scores[MAX_MOVES];
		i32 count = generate_moves(pos, moves, true);
		
		score_moves(pos, moves, scores, count, NullMove, ply);
		
		for (i32 i = 0; i < count; i++) {
			pick_move(moves, scores, count, i);
			
			Position new_pos = pos;
			if (!new_pos.make_move(moves[i])) continue;
			
			i32 score = -quiescence(new_pos, -beta, -alpha, ply + 1, info);
			
			if (stopped.load(std::memory_order_relaxed)) return 0;
			
			if (score >= beta) return beta;
			if (score > alpha) alpha = score;
		}
		
		return alpha;
	}
	
	bool is_repetition(u64 key, i32 ply) {
		
		for (i32 j = game_ply + ply - 2; j >= 0; j -= 2) {
			if (rep_stack[j] == key) {
				return true;
			}
		}
		
		return false;
	}
	
	i32 alpha_beta(Position& pos, i32 depth, i32 alpha, i32 beta, i32 ply, SearchInfo& info, Move* pv, i32& pv_len, bool is_root) {
		pv_len = 0;
		
		if (check_time(info)) return 0;
		
		if (depth <= 0) {
			return quiescence(pos, alpha, beta, ply, info);
		}
		
		info.nodes++;
		u64 key = Zobrist::hash(pos);
		
		rep_stack[game_ply + ply] = key;
		if (!is_root && is_repetition(key, ply)) {
			return 0;
		}
		
		bool pv_node = (beta - alpha) > 1;
		i32 king_sq = BB::lsb(pos.colour[0] & pos.pieces[King]);
		bool in_check = pos.is_attacked(king_sq);
		
		// Check extension
		if (in_check) depth++;
		
		// Mate distance pruning
		i32 mate_value = MATE_SCORE - ply;
		if (mate_value < beta) {
			beta = mate_value;
			if (alpha >= mate_value) return mate_value;
		}
		mate_value = -MATE_SCORE + ply;
		if (mate_value > alpha) {
			alpha = mate_value;
			if (beta <= mate_value) return mate_value;
		}
		
		// TT lookup
		TTEntry* entry = tt.probe(key);
		Move tt_move = NullMove;
		
		if (entry) {
			tt_move = entry->best_move;
			
			if (!is_root && entry->depth >= depth) {
				i32 tt_score = entry->score;
				
				if (tt_score > MATE_SCORE - MAX_PLY) tt_score -= ply;
				else if (tt_score < -MATE_SCORE + MAX_PLY) tt_score += ply;
				
				if (entry->flag == TT_EXACT) return tt_score;
				if (entry->flag == TT_ALPHA && tt_score <= alpha) return alpha;
				if (entry->flag == TT_BETA && tt_score >= beta) return beta;
			}
		}
		
		// Static eval for improving heuristic
		i32 static_eval = in_check ? -INF : Eval::evaluate(pos);
		eval_stack[ply] = static_eval;
		bool improving = !in_check && ply >= 2 && static_eval > eval_stack[ply - 2];
		
		// =====================================================
		// Reverse Futility Pruning (Static Null Move Pruning)
		// =====================================================
		if (!pv_node
			&& !in_check
			&& depth < 8
			&& static_eval < MATE_SCORE - MAX_PLY
			&& static_eval >= beta + 70 * depth - 70 * improving) {
			return (static_eval + beta) / 2;
		}
		// =====================================================
// Null Move Pruning 
// =====================================================
		if (!pv_node
			&& !in_check
			&& depth >= 3
			&& static_eval >= beta+20
			&& beta > -MATE_SCORE + MAX_PLY  
			) {
			u64 non_pawn = pos.colour[0] & ~pos.pieces[Pawn] & ~pos.pieces[King];
			
			if (non_pawn) {
				Position null_pos = pos;
				null_pos.ep = 0;      
				null_pos.flip();     
				i32 R = (static_eval - beta + depth * 30 + 480) / 105;
				Move null_pv[1];      
				i32 null_pv_len = 0;
				i32 null_score = -alpha_beta(
					null_pos,
					depth - R,        
					-beta, -beta + 1,
					ply + 1,
					info,
					null_pv, null_pv_len,
					false
					);
				
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
		i32 scores[MAX_MOVES];
		i32 count = generate_moves(pos, moves, false);
		
		score_moves(pos, moves, scores, count, tt_move, ply);
		
		i32 legal_moves = 0;
		i32 best_score = -INF;
		Move best_move = NullMove;
		u8 tt_flag = TT_ALPHA;
		
		Move child_pv[MAX_PLY];
		i32 child_pv_len;
		
		Move quiets_tried[MAX_MOVES];
		i32 quiets_count = 0;
		
		for (i32 i = 0; i < count; i++) {
			pick_move(moves, scores, count, i);
			
			Position new_pos = pos;
			if (!new_pos.make_move(moves[i])) continue;
			
			legal_moves++;
			
			bool is_capture = pos.piece_on(moves[i].to) != None;
			bool is_promo = moves[i].promo != None;
			bool is_quiet = !is_capture && !is_promo;
			bool is_killer = (moves[i] == killers[ply][0]) || (moves[i] == killers[ply][1]);
			
			if (is_quiet) {
				quiets_tried[quiets_count++] = moves[i];
			}
			
			i32 score;
			i32 new_depth = depth - 1;
			
			// ============================================
			// PVS (Principal Variation Search)
			// ============================================
			
			if (legal_moves == 1) {
				// First move: full window search (this is expected to be the best move)
				score = -alpha_beta(new_pos, new_depth, -beta, -alpha, ply + 1, info, child_pv, child_pv_len, false);
			} else {
				// Late Move Reduction
				i32 reduction = 0;
				
				if (depth >= 3 && is_quiet && !in_check) {
					reduction = lmr_table[std::min(depth, MAX_PLY - 1)][std::min(legal_moves, MAX_MOVES - 1)];
					
					if (pv_node) reduction--;
					if (improving) reduction--;
					if (is_killer) reduction--;
					
					reduction -= history[moves[i].from][moves[i].to] / 4096;
					reduction = std::clamp(reduction, 0, new_depth - 1);
				}
				
				// PVS(50 Elo)
				score = -alpha_beta(new_pos, new_depth - reduction, -alpha - 1, -alpha, ply + 1, info, child_pv, child_pv_len, false);
				
				// If null window search fails high (score > alpha), we need to re-search
				if (score > alpha && score < beta) {
					// Re-search with full window
					score = -alpha_beta(new_pos, new_depth, -beta, -alpha, ply + 1, info, child_pv, child_pv_len, false);
				}
				// Ifused LMR and it failed high on the null window, verify with full depth
				else if (score > alpha && reduction > 0) {
					// First verify with full depth but null window
					score = -alpha_beta(new_pos, new_depth, -alpha - 1, -alpha, ply + 1, info, child_pv, child_pv_len, false);
					
					// If still fails high, do full window search
					if (score > alpha && score < beta) {
						score = -alpha_beta(new_pos, new_depth, -beta, -alpha, ply + 1, info, child_pv, child_pv_len, false);
					}
				}
			}
			
			if (stopped.load(std::memory_order_relaxed)) return 0;
			
			if (score > best_score) {
				best_score = score;
				best_move = moves[i];
				
				if (score > alpha) {
					alpha = score;
					tt_flag = TT_EXACT;
					
					pv[0] = moves[i];
					for (i32 j = 0; j < child_pv_len; j++) {
						pv[j + 1] = flip_move(child_pv[j]);
					}
					pv_len = child_pv_len + 1;
					
					if (score >= beta) {
						if (is_quiet) {
							update_killers(ply, moves[i]);
							
							i32 bonus = depth * depth;
							update_history(moves[i].from, moves[i].to, bonus);
							
							for (i32 j = 0; j < quiets_count - 1; j++) {
								update_history(quiets_tried[j].from, quiets_tried[j].to, -bonus);
							}
						}
						
						i32 store_score = best_score;
						if (store_score > MATE_SCORE - MAX_PLY) store_score += ply;
						else if (store_score < -MATE_SCORE + MAX_PLY) store_score -= ply;
						
						tt.store(key, depth, store_score, TT_BETA, best_move);
						return beta;
					}
				}
			}
		}
		
		// Checkmate or stalemate
		if (legal_moves == 0) {
			if (in_check) {
				return -MATE_SCORE + ply;
			} else {
				return 0;
			}
		}
		
		// Store in TT
		i32 store_score = best_score;
		if (store_score > MATE_SCORE - MAX_PLY) store_score += ply;
		else if (store_score < -MATE_SCORE + MAX_PLY) store_score -= ply;
		
		tt.store(key, depth, store_score, tt_flag, best_move);
		
		return best_score;
	}
	
	void print_info(SearchInfo& info, i32 score, const Position& pos) {
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - info.start_time).count();
		
		std::cout << "info depth " << info.depth
		<< " seldepth " << info.seldepth;
		
		if (score > MATE_SCORE - MAX_PLY) {
			i32 mate_in = (MATE_SCORE - score + 1) / 2;
			std::cout << " score mate " << mate_in;
		} else if (score < -MATE_SCORE + MAX_PLY) {
			i32 mate_in = -(MATE_SCORE + score + 1) / 2;
			std::cout << " score mate " << mate_in;
		} else {
			std::cout << " score cp " << score;
		}
		
		std::cout << " nodes " << info.nodes
		<< " time " << elapsed;
		
		if (elapsed > 0) {
			std::cout << " nps " << (info.nodes * 1000 / elapsed);
		}
		
		std::cout << " hashfull " << tt.hashfull();
		
		std::cout << " pv";
		for (i32 i = 0; i < info.pv_length; i++) {
			std::cout << " " << move_to_string(info.pv[i], pos.flipped);
		}
		
		std::cout << std::endl;
	}
	
	void init() {
		stopped.store(false);
		game_ply = 0;
		init_lmr_table();
		clear_tables();
	}
	
	void stop() {
		stopped.store(true, std::memory_order_relaxed);
	}
	
	Move search(Position& pos, SearchInfo& info, i32 max_depth) {
		info.reset();
		info.start_time = std::chrono::steady_clock::now();
		stopped.store(false, std::memory_order_relaxed);
		
		std::memset(killers, 0, sizeof(killers));
		
		Move best_move = NullMove;
		i32 last_score = 0;
		
		for (i32 depth = 1; depth <= max_depth; depth++) {
			info.depth = depth;
			info.seldepth = 0;
			
			Move pv[MAX_PLY];
			i32 pv_len = 0;
			
			i32 score;
			// Aspiration windows around the previous iteration's score.
			if (depth >= 4) {
				i32 delta = 18;
				i32 a = last_score - delta;
				i32 b = last_score + delta;
				while (true) {
					score = alpha_beta(pos, depth, a, b, 0, info, pv, pv_len, true);
					if (stopped.load(std::memory_order_relaxed)) break;
					if (score <= a) {
						a -= delta;
						delta = std::min(delta * 2, 2000);
						continue;
					}
					if (score >= b) {
						b += delta;
						delta = std::min(delta * 2, 2000);
						continue;
					}
					break;
				}
			} else {
				score = alpha_beta(pos, depth, -INF, INF, 0, info, pv, pv_len, true);
			}
			
			if (stopped.load(std::memory_order_relaxed) && depth > 1) break;
			
			if (pv_len > 0) {
				best_move = pv[0];
				info.pv_length = pv_len;
				std::memcpy(info.pv, pv, pv_len * sizeof(Move));
			}
			
			print_info(info, score, pos);
			last_score = score;
			
			if (score > MATE_SCORE - MAX_PLY || score < -MATE_SCORE + MAX_PLY) {
				break;
			}
		}
		
		return best_move;
	}
	
} // namespace Search 
