#include "position.h"
#include <iostream>
#include <sstream>

Position::Position() {
	colour[0] = 0x000000000000FFFFULL;
	colour[1] = 0xFFFF000000000000ULL;
	pieces[Pawn]   = 0x00FF00000000FF00ULL;  
	pieces[Knight] = 0x4200000000000042ULL;  
	pieces[Bishop] = 0x2400000000000024ULL;  
	pieces[Rook]   = 0x8100000000000081ULL;  
	pieces[Queen]  = 0x0800000000000008ULL;  
	pieces[King]   = 0x1000000000000010ULL;  
	castling[0] = castling[1] = castling[2] = castling[3] = true;
	ep = 0;
	flipped = false;
}

void Position::set_fen(const std::string& fen) {
	colour[0] = colour[1] = 0;
	for (int i = 0; i < 6; i++) pieces[i] = 0;
	for (int i = 0; i < 4; i++) castling[i] = false;
	ep = 0;
	flipped = false;
	
	std::istringstream ss(fen);
	std::string board_str, side_str, castle_str, ep_str;
	ss >> board_str >> side_str >> castle_str >> ep_str;

	i32 sq = 56; 
	for (char c : board_str) {
		if (c == '/') {
			sq -= 16; 
		} else if (c >= '1' && c <= '8') {
			sq += (c - '0');  
		} else {
			bool is_black = (c >= 'a' && c <= 'z');
			PieceType pt;
			char lower = is_black ? c : (c + 32);
			
			switch (lower) {
				case 'p': pt = Pawn; break;
				case 'n': pt = Knight; break;
				case 'b': pt = Bishop; break;
				case 'r': pt = Rook; break;
				case 'q': pt = Queen; break;
				case 'k': pt = King; break;
				default: pt = None;
			}
			
			if (pt != None) {
				u64 bb = BB::square_bb(sq);
				colour[is_black ? 1 : 0] |= bb;
				pieces[pt] |= bb;
			}
			sq++;
		}
	}
	
	bool black_to_move = (side_str == "b");
	for (char c : castle_str) {
		switch (c) {
			case 'K': castling[0] = true; break;
			case 'Q': castling[1] = true; break;
			case 'k': castling[2] = true; break;
			case 'q': castling[3] = true; break;
		}
	}
	if (ep_str != "-" && ep_str.length() >= 2) {
		i32 file = ep_str[0] - 'a';
		i32 rank = ep_str[1] - '1';
		ep = BB::square_bb(make_square(rank, file));
	}
	if (black_to_move) {
		flip();
	}
}

void Position::flip() {
	flipped = !flipped;
	colour[0] = BB::flip(colour[0]);
	colour[1] = BB::flip(colour[1]);
	std::swap(colour[0], colour[1]);
	
	for (int i = 0; i < 6; i++) {
		pieces[i] = BB::flip(pieces[i]);
	}
	
	ep = BB::flip(ep);
	std::swap(castling[0], castling[2]);
	std::swap(castling[1], castling[3]);
}

PieceType Position::piece_on(i32 sq) const {
	u64 bb = BB::square_bb(sq);
	for (int pt = Pawn; pt < None; pt++) {
		if (pieces[pt] & bb) return static_cast<PieceType>(pt);
	}
	return None;
}

bool Position::is_attacked(i32 sq, bool by_enemy) const {
	int attacker = by_enemy ? 1 : 0;
	u64 attackers = colour[attacker];
	u64 all = all_pieces();
	u64 pawns = attackers & pieces[Pawn];
	u64 pawn_attacks;
	if (by_enemy) {
		pawn_attacks = BB::south_west(pawns) | BB::south_east(pawns);
	} else {
		pawn_attacks = BB::north_west(pawns) | BB::north_east(pawns);
	}
	if (pawn_attacks & BB::square_bb(sq)) return true;
	if (BB::knight_attacks(sq) & attackers & pieces[Knight]) return true;
	if (BB::bishop_attacks(sq, all) & attackers & pieces[Bishop]) return true;
	if (BB::rook_attacks(sq, all) & attackers & pieces[Rook]) return true;
	if (BB::queen_attacks(sq, all) & attackers & pieces[Queen]) return true;
	if (BB::king_attacks(sq) & attackers & pieces[King]) return true;
	
	return false;
}

bool Position::make_move(const Move& move) {
	u64 from_bb = BB::square_bb(move.from);
	u64 to_bb = BB::square_bb(move.to);
	u64 move_mask = from_bb | to_bb;
	
	PieceType piece = piece_on(move.from);
	PieceType captured = piece_on(move.to);

	colour[0] ^= move_mask;
	pieces[piece] ^= move_mask;

	if (captured != None) {
		colour[1] ^= to_bb;
		pieces[captured] ^= to_bb;
	}

	if (piece == Pawn && to_bb == ep) {
		u64 captured_pawn = BB::south(to_bb);
		colour[1] ^= captured_pawn;
		pieces[Pawn] ^= captured_pawn;
	}

	ep = 0;

	if (piece == Pawn && (move.to - move.from == 16)) {
		ep = BB::south(to_bb);
	}

	if (piece == King) {
		if (move.to - move.from == 2) {
			u64 rook_move = BB::square_bb(H1) | BB::square_bb(F1);
			colour[0] ^= rook_move;
			pieces[Rook] ^= rook_move;
		} else if (move.from - move.to == 2) {
			u64 rook_move = BB::square_bb(A1) | BB::square_bb(D1);
			colour[0] ^= rook_move;
			pieces[Rook] ^= rook_move;
		}
	}
	
	if (piece == Pawn && rank_of(move.to) == 7) {
		pieces[Pawn] ^= to_bb;
		pieces[move.promo] ^= to_bb;
	}
	
	if (move_mask & BB::square_bb(E1)) {
		castling[0] = castling[1] = false;  
	}
	if (move_mask & BB::square_bb(H1)) castling[0] = false;  
	if (move_mask & BB::square_bb(A1)) castling[1] = false;  
	if (move_mask & BB::square_bb(E8)) {
		castling[2] = castling[3] = false;  
	}
	if (move_mask & BB::square_bb(H8)) castling[2] = false;  
	if (move_mask & BB::square_bb(A8)) castling[3] = false;  

	flip();

	i32 my_king_sq = BB::lsb(colour[1] & pieces[King]);
	
	return !is_attacked(my_king_sq, false);  
}

void Position::print() const {
	const char* piece_chars = "PNBRQKpnbrqk";
	
	std::cout << "\n";
	for (i32 rank = 7; rank >= 0; rank--) {
		i32 display_rank = flipped ? (7 - rank) : rank;
		std::cout << (display_rank + 1) << " | ";
		
		for (i32 file = 0; file < 8; file++) {
			i32 sq = make_square(rank, file);
			u64 bb = BB::square_bb(sq);
			
			char c = '.';
			for (int pt = Pawn; pt < None; pt++) {
				if (pieces[pt] & bb) {
					bool is_enemy = colour[1] & bb;
					c = piece_chars[pt + (is_enemy ? 6 : 0)];
					break;
				}
			}
			std::cout << c << ' ';
		}
		std::cout << "\n";
	}
	std::cout << "  +----------------\n";
	std::cout << "    a b c d e f g h\n\n";
	
	std::cout << "  Flipped: " << (flipped ? "yes (black to move)" : "no (white to move)") << "\n";
	std::cout << "  Castling: ";
	if (castling[0]) std::cout << "K";
	if (castling[1]) std::cout << "Q";
	if (castling[2]) std::cout << "k";
	if (castling[3]) std::cout << "q";
	if (!castling[0] && !castling[1] && !castling[2] && !castling[3]) std::cout << "-";
	std::cout << "\n";
	
	if (ep) {
		i32 ep_sq = BB::lsb(ep);
		std::cout << "  En passant: " << char('a' + file_of(ep_sq)) << (rank_of(ep_sq) + 1) << "\n";
	}
	std::cout << "\n";
}
