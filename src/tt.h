#ifndef TT_H
#define TT_H

#include "types.h"

struct Position;

enum TTFlag : u8 {
	TT_NONE = 0,
	TT_EXACT = 1,
	TT_ALPHA = 2,
	TT_BETA = 3
};

struct TTEntry {
	u64 key;
	i32 score;
	i16 depth;
	u8 flag;
	Move best_move;
};

class TT {
public:
	TT();
	~TT();
	
	void resize(size_t mb);
	void clear();
	void store(u64 key, i32 depth, i32 score, u8 flag, Move move);
	TTEntry* probe(u64 key);
	int hashfull() const;
	size_t size_mb() const { return num_entries * sizeof(TTEntry) / (1024 * 1024); }
	
private:
	TTEntry* table;
	size_t num_entries;
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
