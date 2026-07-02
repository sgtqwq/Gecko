#ifndef TYPES_H
#define TYPES_H

#include <cstdint>
#include <string>

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i32 = int32_t;
using i16 = int16_t;
using i64 = int64_t;
enum PieceType {
	Pawn,
	Knight, 
	Bishop,
	Rook,
	Queen,
	King,
	None
};
enum Color {
	White,
	Black
};
enum Square : i32 {
	A1, B1, C1, D1, E1, F1, G1, H1,
	A2, B2, C2, D2, E2, F2, G2, H2,
	A3, B3, C3, D3, E3, F3, G3, H3,
	A4, B4, C4, D4, E4, F4, G4, H4,
	A5, B5, C5, D5, E5, F5, G5, H5,
	A6, B6, C6, D6, E6, F6, G6, H6,
	A7, B7, C7, D7, E7, F7, G7, H7,
	A8, B8, C8, D8, E8, F8, G8, H8,
	NoSquare = 64
};

inline i32 rank_of(i32 sq) { return sq >> 3; }
inline i32 file_of(i32 sq) { return sq & 7; }
inline i32 make_square(i32 rank, i32 file) { return (rank << 3) | file; }

struct Move {
	u8 from;
	u8 to;
	u8 promo; 
	
	Move() : from(0), to(0), promo(None) {}
	Move(u8 f, u8 t, u8 p = None) : from(f), to(t), promo(p) {}
	
	bool operator==(const Move& other) const {
		return from == other.from && to == other.to && promo == other.promo;
	}
	
	bool operator!=(const Move& other) const {
		return !(*this == other);
	}
	
	bool is_none() const {
		return from == 0 && to == 0 && promo == None;
	}
};

const Move NullMove{};

inline std::string move_to_string(const Move& move, bool flipped) {
	if (move.is_none()) return "0000";
	
	std::string str;
	str += 'a' + file_of(move.from);
	str += '1' + (rank_of(move.from) ^ (flipped ? 7 : 0));
	str += 'a' + file_of(move.to);
	str += '1' + (rank_of(move.to) ^ (flipped ? 7 : 0));
	
	if (move.promo != None) {
		const char promos[] = "nbrq";
		str += promos[move.promo - Knight];
	}
	
	return str;
}

#endif // TYPES_H
