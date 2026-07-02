#ifndef EVAL_H
#define EVAL_H

#include "types.h"

struct Position;

namespace Eval {
	void init();
	bool is_ready();
	i32 evaluate(const Position& pos);
}

#endif // EVAL_H
