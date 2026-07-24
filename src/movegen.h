#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "types.h"
#include "position.h"

enum class MoveGenType {
	All,
	Noisy,
	Quiet
};

i32 generate_moves(const Position& pos, Move* movelist, MoveGenType type = MoveGenType::All);

// Compatibility overload for existing callers outside the engine.
inline i32 generate_moves(const Position& pos, Move* movelist, bool only_captures) {
	return generate_moves(pos, movelist, only_captures ? MoveGenType::Noisy : MoveGenType::All);
}

u64 perft(Position& pos, i32 depth);
void perft_divide(Position& pos, i32 depth);

#endif // MOVEGEN_H
