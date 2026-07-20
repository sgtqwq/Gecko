#include "search_params.h"
#include "search.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

namespace SearchParams {

std::deque<SearchParam>& parameters() {
	static std::deque<SearchParam> params;
	return params;
}

SearchParam& add_parameter(const char* name, i32 value, i32 min_value, i32 max_value,
	double c_end, std::function<void()> callback) {
	parameters().push_back({name, value, value, min_value, max_value, c_end, std::move(callback)});
	return parameters().back();
}

bool set_parameter(const std::string& name, const std::string& value, std::string& error) {
	auto it = std::find_if(parameters().begin(), parameters().end(),
		[&name](const SearchParam& param) { return param.name == name; });

	if (it == parameters().end()) {
		return false;
	}

	try {
		size_t consumed = 0;
		const long long parsed = std::stoll(value, &consumed);
		if (consumed != value.size()) {
			throw std::invalid_argument("trailing characters");
		}
		if (parsed < it->min_value || parsed > it->max_value) {
			std::ostringstream message;
			message << "value " << parsed << " is outside [" << it->min_value
				<< ", " << it->max_value << "]";
			error = message.str();
			return true;
		}

		it->value = static_cast<i32>(parsed);
		if (it->callback) {
			it->callback();
		}
	} catch (const std::exception&) {
		error = "expected an integer";
	}

	return true;
}

void print_uci_options(std::ostream& out) {
	for (const SearchParam& param : parameters()) {
		out << "option name " << param.name
			<< " type spin default " << param.default_value
			<< " min " << param.min_value
			<< " max " << param.max_value << '\n';
	}
}

void print_openbench_config(std::ostream& out) {
	for (const SearchParam& param : parameters()) {
		out << param.name << ", int, " << param.default_value << ", "
			<< param.min_value << ", " << param.max_value << ", ";

		if (param.c_end == static_cast<long long>(param.c_end)) {
			out << static_cast<long long>(param.c_end);
		} else {
			out << std::fixed << std::setprecision(4) << param.c_end << std::defaultfloat;
		}

		out << ", 0.002\n";
	}
}

void update_lmr_table() {
	Search::update_lmr_table();
}

} // namespace SearchParams
