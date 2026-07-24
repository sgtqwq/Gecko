#include "see.h"
#include "bitboard.h"

#include <cstdlib>

namespace SEE {
	namespace {
		u64 minke_lsb(u64 bb, bool flipped) {
			if (!flipped)
				return bb & (~bb + 1);

			const i32 highest_rank = rank_of(63 - __builtin_clzll(bb));
			const u64 rank_mask = 0xFFULL << (highest_rank * 8);
			const u64 candidates = bb & rank_mask;
			return candidates & (~candidates + 1);
		}

		u64 attackers_to(const Position& pos, i32 sq, u64 occupancy) {
			const u64 target = BB::square_bb(sq);
			u64 attackers = 0;

			// colour[0] pawns move north; colour[1] pawns move south.
			attackers |= (BB::south_east(target) | BB::south_west(target))
				& pos.colour[0] & pos.pieces[Pawn];
			attackers |= (BB::north_east(target) | BB::north_west(target))
				& pos.colour[1] & pos.pieces[Pawn];
			attackers |= BB::knight_attacks(sq) & pos.pieces[Knight];
			attackers |= BB::bishop_attacks(sq, occupancy)
				& (pos.pieces[Bishop] | pos.pieces[Queen]);
			attackers |= BB::rook_attacks(sq, occupancy)
				& (pos.pieces[Rook] | pos.pieces[Queen]);
			attackers |= BB::king_attacks(sq) & pos.pieces[King];
			return attackers;
		}
	}

	PieceType captured_piece(const Position& pos, const Move& move) {
		PieceType victim = pos.piece_on(move.to);
		if (victim == None && pos.piece_on(move.from) == Pawn &&
			pos.ep && BB::square_bb(move.to) == pos.ep) {
			victim = Pawn;
		}
		return victim;
	}

	bool is_capture(const Position& pos, const Move& move) {
		return captured_piece(pos, move) != None;
	}

	bool is_noisy(const Position& pos, const Move& move) {
		return move.promo != None || is_capture(pos, move);
	}

	bool ge(const Position& pos, const Move& move, i32 threshold) {
		const PieceType attacker = pos.piece_on(move.from);
		if (attacker == None)
			return false;

		// Castling never changes material.
		if (attacker == King && std::abs(static_cast<i32>(move.to) - static_cast<i32>(move.from)) == 2)
			return threshold <= 0;

		const PieceType target = captured_piece(pos, move);
		i32 score = PieceValue[target] - threshold;
		if (move.promo != None)
			score += PieceValue[move.promo] - PieceValue[Pawn];
		if (score < 0)
			return false;

		score -= move.promo != None ? PieceValue[move.promo] : PieceValue[attacker];
		if (score >= 0)
			return true;

		u64 occupancy = pos.all_pieces();
		u64 attackers = attackers_to(pos, move.to, occupancy);
		occupancy ^= BB::square_bb(move.from);

		const u64 diagonal_attackers = pos.pieces[Bishop] | pos.pieces[Queen];
		const u64 line_attackers = pos.pieces[Rook] | pos.pieces[Queen];
		i32 stm = 1; // The opponent recaptures first.

		while (true) {
			attackers &= occupancy;
			u64 my_attackers = attackers & pos.colour[stm];
			if (!my_attackers)
				break;

			PieceType cheapest = Pawn;
			for (; cheapest <= King; cheapest = static_cast<PieceType>(cheapest + 1)) {
				my_attackers = attackers & pos.colour[stm] & pos.pieces[cheapest];
				if (my_attackers)
					break;
			}

			stm ^= 1;
			score = -score - PieceValue[cheapest] - 1;

			if (score >= 0) {
				if (cheapest == King && (attackers & pos.colour[stm ^ 1]))
					stm ^= 1;
				break;
			}

			const u64 used = minke_lsb(my_attackers, pos.flipped);
			occupancy ^= used;

			switch (cheapest) {
				case Pawn:
				case Bishop:
					attackers |= BB::bishop_attacks(move.to, occupancy) & diagonal_attackers;
					break;
				case Rook:
					attackers |= BB::rook_attacks(move.to, occupancy) & line_attackers;
					break;
				case Queen:
					attackers |= BB::bishop_attacks(move.to, occupancy) & diagonal_attackers;
					attackers |= BB::rook_attacks(move.to, occupancy) & line_attackers;
					break;
				default:
					break;
			}
		}

		return stm != 0;
	}
}
