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
		const u64 all = pos.all_pieces();
		while (pieces) {
			const i32 from = BB::pop_lsb(pieces);
			u64 attacks = 0;

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
				const i32 to = BB::pop_lsb(attacks);
				movelist[count++] = Move(from, to, None);
			}
		}
	}
}

i32 generate_moves(const Position& pos, Move* movelist, MoveGenType type) {
	i32 count = 0;

	const u64 all = pos.all_pieces();
	const u64 us = pos.colour[0];
	const u64 them = pos.colour[1];
	const u64 pawns = us & pos.pieces[Pawn];

	const bool generate_noisy = type != MoveGenType::Quiet;
	const bool generate_quiet = type != MoveGenType::Noisy;

	if (generate_quiet) {
		// Quiet generation excludes promotions: every promotion is tactical/noisy.
		u64 push1 = BB::north(pawns) & ~all & ~BB::Rank8;
		generate_pawn_moves(movelist, count, push1, -8);

		u64 push2 = BB::north(push1 & BB::Rank3) & ~all;
		generate_pawn_moves(movelist, count, push2, -16);
	}

	if (generate_noisy) {
		const u64 capture_targets = them | pos.ep;
		u64 capture_nw = BB::north_west(pawns) & capture_targets;
		u64 capture_ne = BB::north_east(pawns) & capture_targets;
		generate_pawn_moves(movelist, count, capture_nw, -7);
		generate_pawn_moves(movelist, count, capture_ne, -9);

		// Non-capturing promotions belong to the noisy stage as well.
		u64 promo_push = BB::north(pawns & BB::Rank7) & ~all;
		generate_pawn_moves(movelist, count, promo_push, -8);
	}

	u64 piece_to_mask = 0;
	if (type == MoveGenType::All)
		piece_to_mask = ~us;
	else if (type == MoveGenType::Noisy)
		piece_to_mask = them;
	else
		piece_to_mask = ~all;

	generate_piece_moves<Knight>(pos, movelist, count, piece_to_mask);
	generate_piece_moves<Bishop>(pos, movelist, count, piece_to_mask);
	generate_piece_moves<Rook>(pos, movelist, count, piece_to_mask);
	generate_piece_moves<Queen>(pos, movelist, count, piece_to_mask);
	generate_piece_moves<King>(pos, movelist, count, piece_to_mask);

	if (generate_quiet) {
		const i32 king_sq = BB::lsb(us & pos.pieces[King]);
		const u64 rooks = us & pos.pieces[Rook];

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
	const i32 num_moves = generate_moves(pos, movelist, MoveGenType::All);
	u64 nodes = 0;

	for (i32 i = 0; i < num_moves; i++) {
		Position new_pos = pos;
		if (new_pos.make_move(movelist[i]))
			nodes += perft(new_pos, depth - 1);
	}

	return nodes;
}

void perft_divide(Position& pos, i32 depth) {
	Move movelist[256];
	const i32 num_moves = generate_moves(pos, movelist, MoveGenType::All);
	u64 total = 0;

	for (i32 i = 0; i < num_moves; i++) {
		Position new_pos = pos;
		if (new_pos.make_move(movelist[i])) {
			const u64 nodes = (depth > 1) ? perft(new_pos, depth - 1) : 1;
			total += nodes;
			std::cout << move_to_string(movelist[i], pos.flipped) << ": " << nodes << "\n";
		}
	}

	std::cout << "\nTotal: " << total << "\n";
}
