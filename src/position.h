#ifndef POSITION_H
#define POSITION_H

#include "types.h"
#include "bitboard.h"
#include <string>

struct Position {
	u64 colour[2];
	u64 pieces[6];
	bool castling[4];
	u64 ep;
	bool flipped;
	Position();
	void set_fen(const std::string& fen);
	void flip();
	PieceType piece_on(i32 sq) const;
	u64 all_pieces() const { return colour[0] | colour[1]; }
	bool is_attacked(i32 sq, bool by_enemy = true) const;
	bool make_move(const Move& move);
	void print() const;
};

#endif // POSITION_H
