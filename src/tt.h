#ifndef TT_H
#define TT_H

#include "types.h"

#include <cstddef>
#include <limits>

struct Position;

// TT_NONE marks an unused slot. TT_EVAL marks an entry that currently only
// contains a cached static evaluation and therefore cannot cause a cutoff.
enum TTFlag : u8 {
	TT_NONE  = 0,
	TT_EXACT = 1,
	TT_ALPHA = 2,
	TT_BETA  = 3,
	TT_EVAL  = 4
};

constexpr i16 TT_NO_SCORE = std::numeric_limits<i16>::min();

// A move needs 6 + 6 + 3 bits (from, to, promotion), so storing the original
// three-byte Move object would waste space through padding. Compressing it to
// 16 bits lets the complete entry fit in exactly 10 bytes.
struct TTEntry {
	u16 key;
	i16 score;
	i16 static_eval;
	u16 packed_move;
	u8  depth;
	u8  flag;

	Move best_move() const;
	bool has_score() const { return score != TT_NO_SCORE; }
	bool has_static_eval() const { return static_eval != TT_NO_SCORE; }
};

static_assert(sizeof(TTEntry) == 10, "TTEntry must remain 10 bytes");
struct alignas(32) TTBucket {
	TTEntry entries[3];
	u8 padding[2];
};

static_assert(sizeof(TTBucket) == 32, "TTBucket must occupy 32 bytes");

class TT {
public:
	TT();
	~TT();
	
	void resize(size_t mb);
	void clear();
	void store(u64 key, i32 depth, i32 score, i32 static_eval, u8 flag, Move move);
	void store_eval(u64 key, i32 static_eval);
	TTEntry* probe(u64 key);
	void prefetch(u64 key) const;
	int hashfull() const;
	size_t size_mb() const { return num_buckets * sizeof(TTBucket) / (1024 * 1024); }
	
private:
	static u16 pack_move(const Move& move);
	static u16 signature(u64 key) { return static_cast<u16>(key >> 48); }
	size_t index(u64 key) const { return static_cast<size_t>(key) & (num_buckets - 1); }
	TTBucket& bucket(u64 key) { return table[index(key)]; }
	const TTBucket& bucket(u64 key) const { return table[index(key)]; }

	TTBucket* table;
	size_t num_buckets;
	size_t used;
};

namespace Zobrist {
	extern u64 piece_keys[2][6][64];
	extern u64 castle_keys[16];
	extern u64 ep_keys[8];
	
	void init();
	u64 hash(const Position& pos);
}

extern TT tt;

#endif // TT_H
