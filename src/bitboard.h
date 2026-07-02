#ifndef BITBOARD_H
#define BITBOARD_H

#include "types.h"

namespace BB {
	constexpr u64 FileA = 0x0101010101010101ULL;
	constexpr u64 FileB = FileA << 1;
	constexpr u64 FileC = FileA << 2;
	constexpr u64 FileD = FileA << 3;
	constexpr u64 FileE = FileA << 4;
	constexpr u64 FileF = FileA << 5;
	constexpr u64 FileG = FileA << 6;
	constexpr u64 FileH = FileA << 7;
	
	constexpr u64 Rank1 = 0x00000000000000FFULL;
	constexpr u64 Rank2 = Rank1 << 8;
	constexpr u64 Rank3 = Rank1 << 16;
	constexpr u64 Rank4 = Rank1 << 24;
	constexpr u64 Rank5 = Rank1 << 32;
	constexpr u64 Rank6 = Rank1 << 40;
	constexpr u64 Rank7 = Rank1 << 48;
	constexpr u64 Rank8 = Rank1 << 56;
	
	constexpr u64 NotFileA = ~FileA;
	constexpr u64 NotFileH = ~FileH;
	
	inline i32 lsb(u64 bb) {
		return __builtin_ctzll(bb);
	}
	
	inline i32 pop_lsb(u64& bb) {
		i32 sq = lsb(bb);
		bb &= bb - 1;
		return sq;
	}
	
	inline i32 popcount(u64 bb) {
		return __builtin_popcountll(bb);
	}
	
	inline u64 flip(u64 bb) {
		return __builtin_bswap64(bb);
	}
	
	inline u64 square_bb(i32 sq) {
		return 1ULL << sq;
	}
	

	inline u64 north(u64 bb) { return bb << 8; }
	inline u64 south(u64 bb) { return bb >> 8; }
	inline u64 east(u64 bb)  { return (bb << 1) & NotFileA; }
	inline u64 west(u64 bb)  { return (bb >> 1) & NotFileH; }
	
	inline u64 north_east(u64 bb) { return (bb << 9) & NotFileA; }
	inline u64 north_west(u64 bb) { return (bb << 7) & NotFileH; }
	inline u64 south_east(u64 bb) { return (bb >> 7) & NotFileA; }
	inline u64 south_west(u64 bb) { return (bb >> 9) & NotFileH; }
	
	inline u64 knight_attacks(i32 sq) {
		u64 bb = square_bb(sq);
		u64 attacks = 0;
		
		attacks |= (bb << 17) & NotFileA;  
		attacks |= (bb << 15) & NotFileH;  
		attacks |= (bb << 10) & ~(FileA | FileB);  
		attacks |= (bb <<  6) & ~(FileG | FileH);  
		attacks |= (bb >> 17) & NotFileH;  
		attacks |= (bb >> 15) & NotFileA;  
		attacks |= (bb >> 10) & ~(FileG | FileH); 
		attacks |= (bb >>  6) & ~(FileA | FileB);  
		
		return attacks;
	}
	
	inline u64 king_attacks(i32 sq) {
		u64 bb = square_bb(sq);
		u64 attacks = north(bb) | south(bb);
		u64 sides = east(bb) | west(bb);
		attacks |= sides | north(sides) | south(sides);
		return attacks;
	}
	
	template<u64 (*Dir)(u64)>
	inline u64 ray_attacks(i32 sq, u64 blockers) {
		u64 attacks = Dir(square_bb(sq));
		attacks |= Dir(attacks & ~blockers);
		attacks |= Dir(attacks & ~blockers);
		attacks |= Dir(attacks & ~blockers);
		attacks |= Dir(attacks & ~blockers);
		attacks |= Dir(attacks & ~blockers);
		attacks |= Dir(attacks & ~blockers);
		return attacks;
	}
	
	extern u64 DiagMask[64];
	extern u64 AntiDiagMask[64];
	
	inline u64 rook_attacks(i32 sq, u64 blockers) {
		return ray_attacks<north>(sq, blockers) |
		       ray_attacks<south>(sq, blockers) |
		       ray_attacks<east>(sq, blockers)  |
		       ray_attacks<west>(sq, blockers);
	}
	
	inline u64 bishop_attacks(i32 sq, u64 blockers) {
		return ray_attacks<north_east>(sq, blockers) |
		       ray_attacks<north_west>(sq, blockers) |
		       ray_attacks<south_east>(sq, blockers) |
		       ray_attacks<south_west>(sq, blockers);
	}
	
	inline u64 queen_attacks(i32 sq, u64 blockers) {
		return rook_attacks(sq, blockers) | bishop_attacks(sq, blockers);
	}
	
	void init();
	
	void print(u64 bb);
	
} // namespace BB

#endif // BITBOARD_H
