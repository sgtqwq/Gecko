#include "nnue.h"

#include "position.h"
#include "bitboard.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <vector>

#ifdef OPENBENCH_EMBED_NNUE
// Provided by embedded_nnue.S when building with EVALFILE=
extern const unsigned char ob_nnue_data[];
extern const unsigned char ob_nnue_end[];
#endif

namespace NNUE {
	

	constexpr int INPUT_SIZE = 768;
	
	constexpr int HIDDEN_SIZE = 32;
	

	constexpr int QA = 255;
	constexpr int QB = 64;
	

	constexpr int SCALE = 400;
	
	struct Network {
		// feature_weights[feature][hidden]
		alignas(64) i16 feature_weights[INPUT_SIZE][HIDDEN_SIZE];
		alignas(64) i16 feature_bias[HIDDEN_SIZE];
		i16 output_weights[2 * HIDDEN_SIZE];
		i16 output_bias;
	};
	
	static Network g_net{};
	static bool g_loaded = false;
	
	static inline i16 read_i16_le(const uint8_t* p) {
		// Little-endian decode (portable, avoids type-punning).
		uint16_t u = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
		return static_cast<i16>(u);
	}
	
	static inline i32 screlu(i32 x) {
		// Square Clipped ReLU.
		// Range after clamp: 0..QA, squared => 0..QA*QA
		x = std::clamp(x, 0, QA);
		return x * x;
	}

	static bool load_from_buffer(const uint8_t* data, size_t size) {
		if (!data || size == 0) {
			g_loaded = false;
			return false;
		}

		// Minimal required bytes (without Bullet padding).
		constexpr size_t L0W_BYTES = static_cast<size_t>(INPUT_SIZE) * HIDDEN_SIZE * sizeof(i16);
		constexpr size_t L0B_BYTES = static_cast<size_t>(HIDDEN_SIZE) * sizeof(i16);
		constexpr size_t L1W_BYTES = static_cast<size_t>(2 * HIDDEN_SIZE) * sizeof(i16);
		constexpr size_t L1B_BYTES = sizeof(i16);
		constexpr size_t MIN_BYTES = L0W_BYTES + L0B_BYTES + L1W_BYTES + L1B_BYTES;

		if (size < MIN_BYTES) {
			g_loaded = false;
			return false;
		}

		// Parse sequentially.
		size_t off = 0;

		// l0w: feature_weights[feature][hidden]
		for (int feat = 0; feat < INPUT_SIZE; ++feat) {
			for (int h = 0; h < HIDDEN_SIZE; ++h) {
				g_net.feature_weights[feat][h] = read_i16_le(&data[off]);
				off += sizeof(i16);
			}
		}

		// l0b: feature_bias[hidden]
		for (int h = 0; h < HIDDEN_SIZE; ++h) {
			g_net.feature_bias[h] = read_i16_le(&data[off]);
			off += sizeof(i16);
		}

		// l1w: output_weights[2*hidden]
		for (int i = 0; i < 2 * HIDDEN_SIZE; ++i) {
			g_net.output_weights[i] = read_i16_le(&data[off]);
			off += sizeof(i16);
		}

		// l1b: output_bias
		g_net.output_bias = read_i16_le(&data[off]);
		off += sizeof(i16);

		// Any remaining bytes are Bullet padding to 64-byte boundary.
		// We intentionally ignore them.

		g_loaded = true;
		return true;
	}
	
	bool load_from_file(const std::string& path) {
		std::ifstream fin(path, std::ios::binary);
		if (!fin) {
			g_loaded = false;
			return false;
		}
		
		fin.seekg(0, std::ios::end);
		std::streamsize size = fin.tellg();
		fin.seekg(0, std::ios::beg);
		
		if (size <= 0) {
			g_loaded = false;
			return false;
		}
		
		std::vector<uint8_t> buf(static_cast<size_t>(size));
		if (!fin.read(reinterpret_cast<char*>(buf.data()), size)) {
			g_loaded = false;
			return false;
		}
		
		return load_from_buffer(buf.data(), buf.size());
	}

	bool load_from_embedded() {
	#ifdef OPENBENCH_EMBED_NNUE
		const size_t size = static_cast<size_t>(ob_nnue_end - ob_nnue_data);
		return load_from_buffer(reinterpret_cast<const uint8_t*>(ob_nnue_data), size);
	#else
		g_loaded = false;
		return false;
	#endif
	}
	
	bool is_ready() {
		return g_loaded;
	}
	
	// Map a (piece_type, square, is_opponent) triple into (stm_idx, ntm_idx)
	// following Bullet's Chess768::map_features.
	//
	// IMPORTANT: This engine stores Position already flipped so that
	// colour[0] is side-to-move and the board is from STM perspective.
	// Therefore we treat:
	//   - our pieces   => c = 0
	//   - enemy pieces => c = 1
	static inline void chess768_indices(int pt, int sq, bool is_opponent, int& stm_idx, int& ntm_idx) {
		const int c = is_opponent ? 1 : 0;
		const int pc = 64 * pt;
		const int sq_flip = sq ^ 56; // vertical flip (A1<->A8)
		
		// STM stream: [our pieces (0..383)] + [opp pieces (384..767)]
		stm_idx = (c ? 384 : 0) + pc + sq;
		
		// NTM stream: swap colours and flip squares
		ntm_idx = (c ? 0 : 384) + pc + sq_flip;
	}
	
	i32 evaluate(const Position& pos) {
		// Safety: if NNUE is not loaded, return 0 so engine stays usable.
		if (!g_loaded) return 0;
		
		// Build both accumulators from scratch.
		// Use i32 accumulators to avoid any signed overflow UB in C++.
		i32 acc_stm[HIDDEN_SIZE];
		i32 acc_ntm[HIDDEN_SIZE];
		
		for (int i = 0; i < HIDDEN_SIZE; ++i) {
			acc_stm[i] = static_cast<i32>(g_net.feature_bias[i]);
			acc_ntm[i] = static_cast<i32>(g_net.feature_bias[i]);
		}
		
		// Iterate all pieces.
		// Note: Chess768 max_active is 32; a legal chess position never exceeds 32 pieces.
		for (int pt = Pawn; pt <= King; ++pt) {
			// Our pieces (side-to-move)
			u64 bb_us = pos.colour[0] & pos.pieces[pt];
			while (bb_us) {
				const int sq = BB::pop_lsb(bb_us);
				int stm_idx = 0, ntm_idx = 0;
				chess768_indices(pt, sq, /*is_opponent=*/false, stm_idx, ntm_idx);
				
				// Add feature column to both accumulators.
				const i16* w_stm = g_net.feature_weights[stm_idx];
				const i16* w_ntm = g_net.feature_weights[ntm_idx];
				for (int h = 0; h < HIDDEN_SIZE; ++h) {
					acc_stm[h] += static_cast<i32>(w_stm[h]);
					acc_ntm[h] += static_cast<i32>(w_ntm[h]);
				}
			}
			
			// Enemy pieces
			u64 bb_them = pos.colour[1] & pos.pieces[pt];
			while (bb_them) {
				const int sq = BB::pop_lsb(bb_them);
				int stm_idx = 0, ntm_idx = 0;
				chess768_indices(pt, sq, /*is_opponent=*/true, stm_idx, ntm_idx);
				
				const i16* w_stm = g_net.feature_weights[stm_idx];
				const i16* w_ntm = g_net.feature_weights[ntm_idx];
				for (int h = 0; h < HIDDEN_SIZE; ++h) {
					acc_stm[h] += static_cast<i32>(w_stm[h]);
					acc_ntm[h] += static_cast<i32>(w_ntm[h]);
				}
			}
		}
		
		// Hidden (SCReLU) -> output.
		// Use i64 accumulation to be extra safe.
		i64 out = 0;
		
		// STM accumulator uses the first half of output weights.
		for (int h = 0; h < HIDDEN_SIZE; ++h) {
			out += static_cast<i64>(screlu(acc_stm[h])) * static_cast<i64>(g_net.output_weights[h]);
		}
		
		// NTM accumulator uses the second half.
		for (int h = 0; h < HIDDEN_SIZE; ++h) {
			out += static_cast<i64>(screlu(acc_ntm[h])) * static_cast<i64>(g_net.output_weights[HIDDEN_SIZE + h]);
		}
		
		// Reduce quantisation from QA*QA*QB to QA*QB.
		out /= QA;
		
		// Add bias (quantised as QA*QB).
		out += static_cast<i64>(g_net.output_bias);
		
		// Apply evaluation scale (centipawns).
		out *= SCALE;
		
		// Remove quantisation altogether.
		out /= static_cast<i64>(QA) * static_cast<i64>(QB);
		
		return static_cast<i32>(out);
	}
	
} // namespace NNUE
