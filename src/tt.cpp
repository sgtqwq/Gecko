#include "tt.h"
#include "position.h"
#include "bitboard.h"
#include <cstring>
#include <random>
#include <iostream>

TT tt;

namespace Zobrist {
	u64 piece_keys[2][6][64];
	u64 castle_keys[16];
	u64 ep_keys[8];
	
	void init() {
		std::mt19937_64 rng(0x1234567890ABCDEF);
		
		for (int c = 0; c < 2; c++) {
			for (int pt = 0; pt < 6; pt++) {
				for (int sq = 0; sq < 64; sq++) {
					piece_keys[c][pt][sq] = rng();
				}
			}
		}
		
		for (int i = 0; i < 16; i++) {
			castle_keys[i] = rng();
		}
		
		for (int i = 0; i < 8; i++) {
			ep_keys[i] = rng();
		}
	}
	
	u64 hash(const Position& pos) {
		u64 h = 0;
		
		for (int pt = Pawn; pt <= King; pt++) {
			u64 our = pos.colour[0] & pos.pieces[pt];
			while (our) {
				i32 sq = BB::pop_lsb(our);
				h ^= piece_keys[0][pt][sq];
			}
			
			u64 their = pos.colour[1] & pos.pieces[pt];
			while (their) {
				i32 sq = BB::pop_lsb(their);
				h ^= piece_keys[1][pt][sq];
			}
		}
		
		int castle_idx = (pos.castling[0] ? 1 : 0) |
		(pos.castling[1] ? 2 : 0) |
		(pos.castling[2] ? 4 : 0) |
		(pos.castling[3] ? 8 : 0);
		h ^= castle_keys[castle_idx];
		
		if (pos.ep) {
			int ep_file = file_of(BB::lsb(pos.ep));
			h ^= ep_keys[ep_file];
		}
		
		return h;
	}
}

TT::TT() : table(nullptr), num_entries(0), used(0) {
	resize(16);
}

TT::~TT() {
	delete[] table;
}

void TT::resize(size_t mb) {
	delete[] table;
	table = nullptr;
	
	size_t bytes = mb * 1024ULL * 1024ULL;
	num_entries = bytes / sizeof(TTEntry);
	
	if (num_entries == 0) num_entries = 1;
	
	table = new TTEntry[num_entries](); 
	used = 0;
	
	size_t actual_mb = (num_entries * sizeof(TTEntry)) / (1024 * 1024);
	std::cout << "info string Hash table: " << num_entries << " entries (" 
	<< actual_mb << " MB, entry size " << sizeof(TTEntry) << " bytes)" << std::endl;
}

void TT::clear() {
	if (table && num_entries > 0) {
		std::memset(table, 0, num_entries * sizeof(TTEntry));
	}
	used = 0;
}

void TT::store(u64 key, i32 depth, i32 score, u8 flag, Move move) {
	size_t idx = key % num_entries;
	TTEntry* entry = &table[idx];
	
	bool was_empty = (entry->key == 0);
	
	if (was_empty || entry->key == key || depth >= entry->depth) {
		if (was_empty) used++;
		
		entry->key = key;
		entry->depth = depth;
		entry->score = score;
		entry->flag = flag;
		entry->best_move = move;
	}
}

TTEntry* TT::probe(u64 key) {
	size_t idx = key % num_entries;
	TTEntry* entry = &table[idx];
	
	if (entry->key == key) {
		return entry;
	}
	return nullptr;
}

int TT::hashfull() const {
	if (num_entries == 0) return 0;
	return static_cast<int>((used * 1000ULL) / num_entries);
}
