/*
 * rebuild
 * Copyright (c) aceinet
 * License: GPL-2.0
 */
#include "rebuild.h"

int main(int argc, char **argv) {
	// rebuild goes re-compiling!
	rebuild_targets.push_back(CTarget::create("rebuild", {"rebuild.cpp"}, "g++ -o #OUT #DEPENDS"));
	return 0;
}
