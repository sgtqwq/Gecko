#include "types.h"
#include "bitboard.h"
#include "position.h"
#include "movegen.h"
#include "eval.h"
#include "search.h"
#include "tt.h"
#include "uci.h"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

static int run_bench() {
	constexpr i32 BENCH_DEPTH = 15;
	
	Position pos;
	pos.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	
	SearchInfo info;
	info.infinite = true;
	
	auto start = std::chrono::steady_clock::now();
	
	Move best_move = Search::search(pos, info, BENCH_DEPTH);
	
	auto end = std::chrono::steady_clock::now();
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
	if (ms <= 0) ms = 1;
	
	const u64 nps = static_cast<u64>((info.nodes * 1000ULL) / static_cast<u64>(ms));
	
	std::cout << info.nodes << " nodes " << nps << " nps" << std::endl;
	
	return 0;
}

int main(int argc, char* argv[]) {
	BB::init();
	Zobrist::init();
	Eval::init();
	Search::init();
	
	if (argc >= 2 && std::string(argv[1]) == "bench") {
		return run_bench();
	}
	
	UCI::loop();
	return 0;
}
