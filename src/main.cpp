#include "types.h"
#include "bitboard.h"
#include "position.h"
#include "movegen.h"
#include "eval.h"
#include "search.h"
#include "tt.h"
#include "uci.h"
#include <iostream>

int main(int argc, char* argv[]) {
	// Initialize all components
	BB::init();
	Zobrist::init();
	Eval::init();
	Search::init();
	
	// Run UCI loop
	UCI::loop();
	
	return 0;
}
