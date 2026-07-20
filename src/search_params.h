#ifndef SEARCH_PARAMS_H
#define SEARCH_PARAMS_H

#include "types.h"

#include <deque>
#include <functional>
#include <iosfwd>
#include <string>

namespace SearchParams {

struct SearchParam {
	std::string name;
	i32 value;
	i32 default_value;
	i32 min_value;
	i32 max_value;
	double c_end;
	std::function<void()> callback;
};

std::deque<SearchParam>& parameters();
SearchParam& add_parameter(const char* name, i32 value, i32 min_value, i32 max_value,
	double c_end, std::function<void()> callback = {});

bool set_parameter(const std::string& name, const std::string& value, std::string& error);
void print_uci_options(std::ostream& out);
void print_openbench_config(std::ostream& out);

#define SEARCH_PARAM(name, val, min_val, max_val, c_val) \
	inline SearchParam& name##_param = add_parameter(#name, val, min_val, max_val, c_val); \
	inline const i32& name = name##_param.value

#define SEARCH_PARAM_CALLBACK(name, val, min_val, max_val, c_val, callback_fn) \
	inline SearchParam& name##_param = add_parameter( \
		#name, val, min_val, max_val, c_val, callback_fn); \
	inline const i32& name = name##_param.value

// The two LMR formula constants use /1000 fixed point, so 1148 means 1.148
// and 2430 means 2.430. This preserves the original formula exactly.
void update_lmr_table();
SEARCH_PARAM_CALLBACK(lmrBase, 1148, 500, 2000, 110.0, update_lmr_table);
SEARCH_PARAM_CALLBACK(lmrDivisor, 2430, 1500, 4000, 125.0, update_lmr_table);

SEARCH_PARAM(razoringMargin, 320, 250, 650, 10.0);

SEARCH_PARAM(rfpDepthMargin, 88, 50, 120, 8.0);
SEARCH_PARAM(rfpImprovingMargin, 0, -64, 128, 8.0);

SEARCH_PARAM(nmpEvalMargin, 25, 0, 100, 10.0);

// LMP values are in 1/256 move units. Defaults reproduce 7 + depth^2.
SEARCH_PARAM(lmpImpBase, 1792, 128, 3072, 64.0);
SEARCH_PARAM(lmpImpDepth, 256, 64, 512, 48.0);
SEARCH_PARAM(lmpNonImpBase, 1792, 128, 3072, 64.0);
SEARCH_PARAM(lmpNonImpDepth, 256, 64, 512, 48.0);

SEARCH_PARAM(lmrHistoryDivisor, 8192, 4096, 16384, 512.0);
SEARCH_PARAM(lmrPvReduction, 1024, 0, 3072, 256.0);
SEARCH_PARAM(lmrImprovingReduction, 512, 0, 3072, 256.0);
SEARCH_PARAM(lmrInCheckReduction, 512, 0, 3072, 256.0);
SEARCH_PARAM(lmrChildInCheckReduction, 512, 0, 3072, 256.0);

SEARCH_PARAM(aspInitDelta, 25, 5, 50, 2.0);

#undef SEARCH_PARAM
#undef SEARCH_PARAM_CALLBACK

} // namespace SearchParams

#endif // SEARCH_PARAMS_H
