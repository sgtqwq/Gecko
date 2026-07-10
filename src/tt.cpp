#include "tt.h"
#include "position.h"
#include "bitboard.h"
#include <algorithm>
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
		
		for (int c = 0; c < 2; c++)
			for (int pt = 0; pt < 6; pt++)
				for (int sq = 0; sq < 64; sq++)
					piece_keys[c][pt][sq] = rng();
		
		for (int i = 0; i < 16; i++) castle_keys[i] = rng();
		for (int i = 0; i < 8;  i++) ep_keys[i]     = rng();
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
			const u64 ep_capturers = (BB::south_east(pos.ep) | BB::south_west(pos.ep))
			& pos.colour[0] & pos.pieces[Pawn];
			if (ep_capturers)
				h ^= ep_keys[file_of(BB::lsb(pos.ep))];
		}
		
		return h;
	}
}

TT::TT() : table(nullptr), num_buckets(0), used(0) {
	resize(16);
}

TT::~TT() {
	delete[] table;
}

u16 TT::pack_move(const Move& move) {
	return static_cast<u16>((move.from & 63U)
		| ((move.to & 63U) << 6)
		| ((move.promo & 7U) << 12));
}

Move TTEntry::best_move() const {
	return Move(
		static_cast<u8>(packed_move & 63U),
		static_cast<u8>((packed_move >> 6) & 63U),
		static_cast<u8>((packed_move >> 12) & 7U)
	);
}

void TT::resize(size_t mb) {
	delete[] table;
	table = nullptr;
	
	const size_t requested = std::max<size_t>(1, mb * 1024ULL * 1024ULL / sizeof(TTBucket));
	num_buckets = 1;
	while (num_buckets <= requested / 2)
		num_buckets *= 2;
	
	table = new TTBucket[num_buckets];
	clear();
	
	const size_t actual_mb = num_buckets * sizeof(TTBucket) / (1024 * 1024);
	std::cout << "info string Hash table: " << num_buckets * 3 << " entries ("
	<< actual_mb << " MB, entry size " << sizeof(TTEntry)
	<< " bytes)" << std::endl;
}

void TT::clear() {
	if (table && num_buckets > 0) {
		std::memset(table, 0, num_buckets * sizeof(TTBucket));
		for (size_t i = 0; i < num_buckets; ++i) {
			for (TTEntry& entry : table[i].entries) {
				entry.score = TT_NO_SCORE;
				entry.static_eval = TT_NO_SCORE;
			}
		}
	}
	used = 0;
}

void TT::store_eval(u64 key, i32 static_eval) {
	TTBucket& b = bucket(key);
	const u16 sig = signature(key);
	TTEntry* target = nullptr;
	TTEntry* empty = nullptr;
	TTEntry* eval_only = nullptr;
	
	for (TTEntry& entry : b.entries) {
		if (entry.flag != TT_NONE && entry.key == sig) {
			target = &entry;
			break;
		}
		if (entry.flag == TT_NONE && !empty)
			empty = &entry;
		else if (entry.flag == TT_EVAL && !eval_only)
			eval_only = &entry;
	}
	
	if (!target)
		target = empty ? empty : eval_only;
	if (!target)
		return; // Do not evict a searched entry merely to cache an evaluation.
	
	if (target->flag == TT_NONE)
		used++;
	
	if (target->flag == TT_NONE || target->key != sig) {
		target->key = sig;
		target->score = TT_NO_SCORE;
		target->packed_move = pack_move(NullMove);
		target->depth = 0;
		target->flag = TT_EVAL;
	}
	
	target->static_eval = static_cast<i16>(std::clamp(static_eval, -32000, 32000));
}

void TT::store(u64 key, i32 depth, i32 score, i32 static_eval, u8 flag, Move move) {
	TTBucket& b = bucket(key);
	const u16 sig = signature(key);
	TTEntry* target = nullptr;
	TTEntry* empty = nullptr;
	TTEntry* weakest = &b.entries[0];
	
	auto priority = [](const TTEntry& entry) {
		if (entry.flag == TT_NONE) return -1000;
		if (entry.flag == TT_EVAL) return -1;
		return static_cast<int>(entry.depth) + (entry.flag == TT_EXACT ? 2 : 0);
	};
	
	for (TTEntry& entry : b.entries) {
		if (entry.flag != TT_NONE && entry.key == sig) {
			target = &entry;
			break;
		}
		if (entry.flag == TT_NONE && !empty)
			empty = &entry;
		if (priority(entry) < priority(*weakest))
			weakest = &entry;
	}
	
	const bool same_key = target != nullptr;
	if (!target)
		target = empty ? empty : weakest;
	
	if (!same_key && target->flag != TT_NONE) {
		const int incoming_priority = depth + (flag == TT_EXACT ? 2 : 0);
		if (incoming_priority < priority(*target))
			return;
	}
	
	if (target->flag == TT_NONE)
		used++;
	
	if (same_key) {
		// Enrich the matching entry even when its deeper search result is kept.
		if (static_eval != TT_NO_SCORE)
			target->static_eval = static_cast<i16>(std::clamp(static_eval, -32000, 32000));
		if (!move.is_none() && target->best_move().is_none())
			target->packed_move = pack_move(move);
		
		// In particular, never let a depth-0 qsearch result erase a deeper
		// main-search entry for the same position.
		if (target->has_score() && depth < static_cast<i32>(target->depth))
			return;
	}
	
	const i16 saved_static_eval = same_key ? target->static_eval : TT_NO_SCORE;
	const u16 saved_move = same_key ? target->packed_move : pack_move(NullMove);
	
	target->key = sig;
	target->score = score == TT_NO_SCORE
		? TT_NO_SCORE
		: static_cast<i16>(std::clamp(score, -32000, 32000));
	target->static_eval = static_eval == TT_NO_SCORE
		? saved_static_eval
		: static_cast<i16>(std::clamp(static_eval, -32000, 32000));
	target->packed_move = (move.is_none() && same_key) ? saved_move : pack_move(move);
	target->depth = static_cast<u8>(std::clamp(depth, 0, 255));
	target->flag = flag;
}

TTEntry* TT::probe(u64 key) {
	TTBucket& b = bucket(key);
	const u16 sig = signature(key);
	for (TTEntry& entry : b.entries) {
		if (entry.flag != TT_NONE && entry.key == sig)
			return &entry;
	}
	return nullptr;
}

void TT::prefetch(u64 key) const {
#if defined(__GNUC__) || defined(__clang__)
	__builtin_prefetch(&bucket(key), 0, 1);
#else
	(void)key;
#endif
}

int TT::hashfull() const {
	if (num_buckets == 0) return 0;
	return static_cast<int>((used * 1000ULL) / (num_buckets * 3));
}
