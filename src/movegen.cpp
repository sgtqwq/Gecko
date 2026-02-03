#include "movegen.h"
#include "bitboard.h"
#include <iostream>

namespace {
	void generate_pawn_moves(Move* movelist, i32& count, u64 to_mask, i32 offset) {
		while (to_mask) {
			i32 to = BB::pop_lsb(to_mask);
			i32 from = to + offset;
			if (rank_of(to) == 7) {
				movelist[count++] = Move(from, to, Queen);
				movelist[count++] = Move(from, to, Rook);
				movelist[count++] = Move(from, to, Bishop);
				movelist[count++] = Move(from, to, Knight);
			} else {
				movelist[count++] = Move(from, to, None);
			}
		}
	}
	template<PieceType PT>
	void generate_piece_moves(const Position& pos, Move* movelist, i32& count, u64 to_mask) {
		u64 pieces = pos.colour[0] & pos.pieces[PT];
		u64 all = pos.all_pieces();
		while (pieces) {
			i32 from = BB::pop_lsb(pieces);
			u64 attacks;
			
			if constexpr (PT == Knight) {
				attacks = BB::knight_attacks(from);
			} else if constexpr (PT == Bishop) {
				attacks = BB::bishop_attacks(from, all);
			} else if constexpr (PT == Rook) {
				attacks = BB::rook_attacks(from, all);
			} else if constexpr (PT == Queen) {
				attacks = BB::queen_attacks(from, all);
			} else if constexpr (PT == King) {
				attacks = BB::king_attacks(from);
			}
			
			attacks &= to_mask;
			
			while (attacks) {
				i32 to = BB::pop_lsb(attacks);
				movelist[count++] = Move(from, to, None);
			}
		}
	}
	
} // anonymous namespace

i32 generate_moves(const Position& pos, Move* movelist, bool only_captures) {
	i32 count = 0;
	
	u64 all = pos.all_pieces();
	u64 us = pos.colour[0];
	u64 them = pos.colour[1];
	
	u64 to_mask = only_captures ? them : ~us;
	
	u64 pawns = us & pos.pieces[Pawn];
	
	if (!only_captures) {
		u64 push1 = BB::north(pawns) & ~all;
		generate_pawn_moves(movelist, count, push1, -8);
		
		u64 push2 = BB::north(push1 & BB::Rank3) & ~all;
		generate_pawn_moves(movelist, count, push2, -16);
	}
	
	u64 capture_targets = them | pos.ep;
	if (only_captures || capture_targets) {
		u64 capture_nw = BB::north_west(pawns) & capture_targets;
		u64 capture_ne = BB::north_east(pawns) & capture_targets;
		generate_pawn_moves(movelist, count, capture_nw, -7);
		generate_pawn_moves(movelist, count, capture_ne, -9);
	}
	
	if (only_captures) {
		u64 promo_push = BB::north(pawns & BB::Rank7) & ~all;
		generate_pawn_moves(movelist, count, promo_push, -8);
	}
	
	generate_piece_moves<Knight>(pos, movelist, count, to_mask);
	generate_piece_moves<Bishop>(pos, movelist, count, to_mask);
	generate_piece_moves<Rook>(pos, movelist, count, to_mask);
	generate_piece_moves<Queen>(pos, movelist, count, to_mask);
	generate_piece_moves<King>(pos, movelist, count, to_mask);
	
	if (!only_captures) {
		i32 king_sq = BB::lsb(us & pos.pieces[King]);
		u64 rooks = us & pos.pieces[Rook];
		
		if (pos.castling[0] && king_sq == E1) {
			if ((rooks & BB::square_bb(H1)) &&
				!(all & 0x60ULL) &&
				!pos.is_attacked(E1) &&
				!pos.is_attacked(F1) &&
				!pos.is_attacked(G1)) {
				movelist[count++] = Move(E1, G1, None);
			}
		}
		
		if (pos.castling[1] && king_sq == E1) {
			if ((rooks & BB::square_bb(A1)) &&
				!(all & 0x0EULL) &&
				!pos.is_attacked(E1) &&
				!pos.is_attacked(D1) &&
				!pos.is_attacked(C1)) {
				movelist[count++] = Move(E1, C1, None);
			}
		}
	}
	
	return count;
}

u64 perft(Position& pos, i32 depth) {
	if (depth == 0) return 1;
	
	Move movelist[256];
	i32 num_moves = generate_moves(pos, movelist, false);
	
	u64 nodes = 0;
	
	for (i32 i = 0; i < num_moves; i++) {
		Position new_pos = pos;
		
		if (new_pos.make_move(movelist[i])) {
			nodes += perft(new_pos, depth - 1);
		}
	}
	
	return nodes;
}

void perft_divide(Position& pos, i32 depth) {
	Move movelist[256];
	i32 num_moves = generate_moves(pos, movelist, false);
	
	u64 total = 0;
	
	for (i32 i = 0; i < num_moves; i++) {
		Position new_pos = pos;
		
		if (new_pos.make_move(movelist[i])) {
			u64 nodes = (depth > 1) ? perft(new_pos, depth - 1) : 1;
			total += nodes;
			
			std::cout << move_to_string(movelist[i], pos.flipped) 
			<< ": " << nodes << "\n";
		}
	}
	
	std::cout << "\nTotal: " << total << "\n";
}
