#ifndef NNUE_H
#define NNUE_H

#include "types.h"

#include <string>

struct Position;

// Bullet NNUE inference ("examples/simple.rs" format).
//
// Architecture (with dual perspective):
//   (768 -> HIDDEN_SIZE) x2  -> 1
//
// Where the two 768-input streams are:
//   - STM (side-to-move) perspective features
//   - NTM (not-side-to-move) perspective features
//
// The hidden activation is SCReLU (square-clipped ReLU):
//   y = clamp(x, 0, QA)^2
//
// This implementation does NOT do incremental accumulator updates.
// We recompute both accumulators from scratch each evaluation.
namespace NNUE {
	
	// Attempts to load a quantised Bullet network from a file.
	// Returns true on success.
	bool load_from_file(const std::string& path);

	// Load a network that was embedded into the binary at build time.
	// This is enabled by compiling with -DOPENBENCH_EMBED_NNUE and linking
	// an object that provides the symbols ob_nnue_data/ob_nnue_end.
	bool load_from_embedded();
	
	// Returns true if a network has been loaded.
	bool is_ready();
	
	// Evaluate from the side-to-move perspective (positive = good for side to move).
	i32 evaluate(const Position& pos);
}

#endif // NNUE_H
