#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "types.h"
#include "position.h"

i32 generate_moves(const Position& pos, Move* movelist, bool only_captures = false);
u64 perft(Position& pos, i32 depth);
void perft_divide(Position& pos, i32 depth);

#endif // MOVEGEN_H
