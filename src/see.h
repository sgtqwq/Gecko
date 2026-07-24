#ifndef SEE_H
#define SEE_H

#include "position.h"
#include "types.h"

namespace SEE {
	constexpr i32 PieceValue[7] = {100, 297, 297, 509, 995, 5000, 0};

	PieceType captured_piece(const Position& pos, const Move& move);
	bool is_capture(const Position& pos, const Move& move);
	bool is_noisy(const Position& pos, const Move& move);
	bool ge(const Position& pos, const Move& move, i32 threshold);
}

#endif // SEE_H
