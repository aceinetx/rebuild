/*
 * rebuild
 * Copyright (c) aceinet
 * License: GPL-2.0
 */
#include "rebuild.h"

std::string script = R"(
ctarget "rebuild" needs "rebuild.cpp" 
	cmd "g++ -o #OUT #DEPENDS"
)";

int main(int argc, char** argv) {
	// rebuild goes re-compiling!
	// rebuild_targets.push_back(CTarget::create("rebuild", {"rebuild.cpp"}, "g++ -o #OUT #DEPENDS"));
	rescript::do_rescript(script);
	return 0;
}
