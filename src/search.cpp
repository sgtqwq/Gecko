#include "search.h"
#include "movegen.h"
#include "eval.h"
#include "tt.h"
#include "bitboard.h"
#include <iostream>
#include <algorithm>
#include <cstring>

namespace Search {
	
	std::atomic<bool> stopped{false};
	
	const i32 victim_scores[6] = { 100, 300, 310, 500, 900, 10000 };
	const i32 attacker_scores[6] = { 1, 2, 3, 4, 5, 6 };
	
	struct ScoredMove {
		Move move;
		i32 score;
	};
	
	inline Move flip_move(const Move& m) {
		return Move(m.from ^ 56, m.to ^ 56, m.promo);
	}
	
	i32 score_move(const Position& pos, const Move& move, const Move& tt_move) {
		if (move == tt_move) return 100000;
		
		PieceType captured = pos.piece_on(move.to);
		
		if (captured != None) {
			PieceType attacker = pos.piece_on(move.from);
			return 10000 + victim_scores[captured] * 10 - attacker_scores[attacker];
		}
		
		if (move.promo != None) {
			return 9000 + move.promo;
		}
		
		return 0;
	}
	
	void sort_moves(const Position& pos, Move* moves, i32 count, const Move& tt_move) {
		ScoredMove scored[MAX_MOVES];
		
		for (i32 i = 0; i < count; i++) {
			scored[i].move = moves[i];
			scored[i].score = score_move(pos, moves[i], tt_move);
		}
		
		for (i32 i = 0; i < count - 1; i++) {
			i32 best = i;
			for (i32 j = i + 1; j < count; j++) {
				if (scored[j].score > scored[best].score) {
					best = j;
				}
			}
			if (best != i) {
				std::swap(scored[i], scored[best]);
			}
		}
		
		for (i32 i = 0; i < count; i++) {
			moves[i] = scored[i].move;
		}
	}
	
	bool check_time(SearchInfo& info) {
		if (stopped.load(std::memory_order_relaxed)) return true;
		
		if (!info.infinite && info.nodes % 2048 == 0) {
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
		i32 count = generate_moves(pos, moves, true);
		
		sort_moves(pos, moves, count, NullMove);
		
		for (i32 i = 0; i < count; i++) {
			Position new_pos = pos;
			if (!new_pos.make_move(moves[i])) continue;
			
			i32 score = -quiescence(new_pos, -beta, -alpha, ply + 1, info);
			
			if (stopped.load(std::memory_order_relaxed)) return 0;
			
			if (score >= beta) return beta;
			if (score > alpha) alpha = score;
		}
		
		return alpha;
	}
	
	i32 alpha_beta(Position& pos, i32 depth, i32 alpha, i32 beta, i32 ply, SearchInfo& info, Move* pv, i32& pv_len, bool is_root = false) {
		pv_len = 0;
		
		if (check_time(info)) return 0;
		
		if (depth <= 0) {
			return quiescence(pos, alpha, beta, ply, info);
		}
		
		info.nodes++;
		if (ply > info.seldepth) info.seldepth = ply;
		
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
		u64 key = Zobrist::hash(pos);
		TTEntry* entry = tt.probe(key);
		Move tt_move = NullMove;
		
		if (entry) {
			tt_move = entry->best_move;
			
			// Search on root
			if (!is_root && entry->depth >= depth) {
				i32 tt_score = entry->score;
				
				if (tt_score > MATE_SCORE - MAX_PLY) tt_score -= ply;
				else if (tt_score < -MATE_SCORE + MAX_PLY) tt_score += ply;
				
				if (entry->flag == TT_EXACT) return tt_score;
				if (entry->flag == TT_ALPHA && tt_score <= alpha) return alpha;
				if (entry->flag == TT_BETA && tt_score >= beta) return beta;
			}
		}
		
		Move moves[MAX_MOVES];
		i32 count = generate_moves(pos, moves, false);
		
		sort_moves(pos, moves, count, tt_move);
		
		i32 legal_moves = 0;
		i32 best_score = -INF;
		Move best_move = NullMove;
		u8 tt_flag = TT_ALPHA;
		
		Move child_pv[MAX_PLY];
		i32 child_pv_len;
		
		for (i32 i = 0; i < count; i++) {
			Position new_pos = pos;
			if (!new_pos.make_move(moves[i])) continue;
			
			legal_moves++;
			
			i32 score = -alpha_beta(new_pos, depth - 1, -beta, -alpha, ply + 1, info, child_pv, child_pv_len, false);
			
			if (stopped.load(std::memory_order_relaxed)) return 0;
			
			if (score > best_score) {
				best_score = score;
				best_move = moves[i];
				
				if (score > alpha) {
					alpha = score;
					tt_flag = TT_EXACT;
					
					// update PV
					pv[0] = moves[i];
					for (i32 j = 0; j < child_pv_len; j++) {
						pv[j + 1] = flip_move(child_pv[j]);
					}
					pv_len = child_pv_len + 1;
					
					if (score >= beta) {
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
			i32 king_sq = BB::lsb(pos.colour[0] & pos.pieces[King]);
			if (pos.is_attacked(king_sq)) {
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
	}
	
	void stop() {
		stopped.store(true, std::memory_order_relaxed);
	}
	
	Move search(Position& pos, SearchInfo& info, i32 max_depth) {
		info.reset();
		info.start_time = std::chrono::steady_clock::now();
		stopped.store(false, std::memory_order_relaxed);
		
		Move best_move = NullMove;
		
		for (i32 depth = 1; depth <= max_depth; depth++) {
			info.depth = depth;
			info.seldepth = 0;
			
			Move pv[MAX_PLY];
			i32 pv_len = 0;
			
			i32 score = alpha_beta(pos, depth, -INF, INF, 0, info, pv, pv_len, true);
			
			if (stopped.load(std::memory_order_relaxed) && depth > 1) break;
			
			if (pv_len > 0) {
				best_move = pv[0];
				info.pv_length = pv_len;
				std::memcpy(info.pv, pv, pv_len * sizeof(Move));
			}
			
			print_info(info, score, pos);
			
			if (score > MATE_SCORE - MAX_PLY || score < -MATE_SCORE + MAX_PLY) {
				break;
			}
		}
		
		return best_move;
	}
	
} // namespace Search
