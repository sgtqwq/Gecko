#include "bitboard.h"
#include <iostream>

namespace BB {
	
	u64 DiagMask[64];
	u64 AntiDiagMask[64];
	
	void init() {
		for (i32 sq = 0; sq < 64; sq++) {
			i32 r = rank_of(sq);
			i32 f = file_of(sq);
			
			DiagMask[sq] = 0;
			for (i32 tr = r + 1, tf = f + 1; tr < 8 && tf < 8; tr++, tf++) {
				DiagMask[sq] |= square_bb(make_square(tr, tf));
			}
			for (i32 tr = r - 1, tf = f - 1; tr >= 0 && tf >= 0; tr--, tf--) {
				DiagMask[sq] |= square_bb(make_square(tr, tf));
			}
			
			AntiDiagMask[sq] = 0;
			for (i32 tr = r + 1, tf = f - 1; tr < 8 && tf >= 0; tr++, tf--) {
				AntiDiagMask[sq] |= square_bb(make_square(tr, tf));
			}
			for (i32 tr = r - 1, tf = f + 1; tr >= 0 && tf < 8; tr--, tf++) {
				AntiDiagMask[sq] |= square_bb(make_square(tr, tf));
			}
		}
	}
	
	void print(u64 bb) {
		std::cout << "\n";
		for (i32 rank = 7; rank >= 0; rank--) {
			std::cout << (rank + 1) << " | ";
			for (i32 file = 0; file < 8; file++) {
				i32 sq = make_square(rank, file);
				std::cout << ((bb & square_bb(sq)) ? "1 " : ". ");
			}
			std::cout << "\n";
		}
		std::cout << "  +----------------\n";
		std::cout << "    a b c d e f g h\n\n";
		std::cout << "  Hex: 0x" << std::hex << bb << std::dec << "\n";
	}
	
} // namespace BB
