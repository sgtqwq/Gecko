#ifndef EVAL_H
#define EVAL_H

#include "types.h"

struct Position;

namespace Eval {
	void init();
	bool is_ready();

	// Piece-square tables already include base piece values.
	// Indexed by [PieceType][Square] with A1=0.
	const Score& psqt(PieceType pt, i32 sq);
	const i32* phase_increments();

	i32 evaluate(const Position& pos);
}

#endif // EVAL_H
