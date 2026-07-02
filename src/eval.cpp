#include "eval.h"
#include "nnue.h"

#include <iostream>

namespace Eval {

	static bool g_ready = false;

	void init() {
#ifdef OPENBENCH_EMBED_NNUE
		g_ready = NNUE::load_from_embedded();
		if (g_ready) {
			std::cout << "info string NNUE loaded: embedded" << std::endl;
		} else {
			std::cout << "info string Embedded NNUE missing or invalid" << std::endl;
		}
#else
		g_ready = NNUE::load_from_file("nnue.bin");
		if (g_ready) {
			std::cout << "info string NNUE loaded: nnue.bin" << std::endl;
		} else {
			std::cout << "info string NNUE not found or invalid" << std::endl;
		}
#endif
	}

	bool is_ready() {
		return g_ready && NNUE::is_ready();
	}

	i32 evaluate(const Position& pos) {
		return is_ready() ? NNUE::evaluate(pos) : 0;
	}

} // namespace Eval
