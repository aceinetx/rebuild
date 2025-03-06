/*
 * rebuild
 * Copyright (c) aceinet
 * License: GPL-2.0
 */
#pragma once

#ifndef __cplusplus
#error "rebuild requires c++"
#else

#ifndef REBUILD_STANDARD_CXX_COMPILER
#define REBUILD_STANDARD_CXX_COMPILER "g++"
#endif

#include <algorithm>
#include <assert.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <sstream>
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
class Target;

static char *rebuild_path;
static char **rebuild_envp;
static std::vector<Target *> rebuild_targets;
static uint64_t rebuild_last_run_time = 0;
static uint32_t rebuild_built_targets = 0;
static uint32_t rebuild_total_targets = 0;

/*
 * rebuild_fexists
 * ---------------
 * Checks if a file exists
 */
static bool rebuild_fexists(std::string &name) {
	return (access(name.c_str(), F_OK) != -1);
}

/*
 * rebuild_get_modified_date
 * -------------------------
 * Gets last modified date of a file in UNIX time
 */
static uint64_t rebuild_get_modified_date(std::string &filename) {
	namespace fs = std::filesystem;

	try {
		auto ftime = fs::last_write_time(filename);
		auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
		auto unix_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(sctp.time_since_epoch()).count();

		return static_cast<uint64_t>(unix_timestamp);
	} catch (const fs::filesystem_error &e) {
		return 0;
	}
}

/*
 * rebuild_replace_all
 * -------------------
 * Replaces every occurence in &str from &from to &to
 */
std::string rebuild_replace_all(const std::string &str, const std::string &from, const std::string &to) {
	if (from.empty())
		return str; // Avoid empty substring case

	std::string result = str; // Create a copy of the original string
	size_t start_pos = 0;
	while ((start_pos = result.find(from, start_pos)) != std::string::npos) {
		result.replace(start_pos, from.length(), to);
		start_pos += to.length(); // Move past the replaced part
	}
	return result; // Return the modified string
}

/*
 * rebuild_join
 * ------------
 * Joins a string vector into one string
 */
std::string rebuild_join(std::vector<std::string> &vec, const char *delim) {
	std::stringstream res;
	copy(vec.begin(), vec.end(), std::ostream_iterator<std::string>(res, delim));
	return res.str();
}

/*
 * rebuild_ends_with
 * -----------------
 * Checks if a string ends with something (C++ <20 solution)
 */
bool rebuild_ends_with(std::string const &fullString, std::string const &ending) {
	if (fullString.length() >= ending.length()) {
		return (0 == fullString.compare(fullString.length() - ending.length(), ending.length(), ending));
	} else {
		return false;
	}
}

/*
 * rebuild_get_percentage
 * ----------------------
 * Gets current build progress in percentages
 */
static int rebuild_get_percentage() {
	int percent = 100 / rebuild_total_targets * (rebuild_built_targets);

	if (percent > 100)
		percent = 100;
	return percent;
}

/*
 * rebuild_get_included_headers
 * ----------------------------
 * Gets included headers used by source_file (with g++)
 */
static std::vector<std::string> rebuild_get_included_headers(std::string source_file, std::string compiler = REBUILD_STANDARD_CXX_COMPILER, std::string compiler_args = "") {
	std::vector<std::string> headers;
	std::string command = compiler + " -MM " + source_file + " " + compiler_args;
	FILE *pipe = popen(command.c_str(), "r");
	if (!pipe) {
		return headers; // Failed to open pipe
	}

	std::vector<std::string> lines;
	std::string current_line;
	char buffer[256];

	while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
		current_line += buffer;

		// Check if the current line ends with a backslash followed by a newline
		while (true) {
			size_t len = current_line.length();
			if (len >= 2 && current_line[len - 2] == '\\' && current_line[len - 1] == '\n') {
				// Remove the backslash and newline, then continue reading next line
				current_line.erase(len - 2, 2);
				if (!fgets(buffer, sizeof(buffer), pipe)) {
					break; // No more lines to read
				}
				current_line += buffer;
			} else {
				break; // Line does not end with a backslash
			}
		}

		// Add the processed line (merged continuation lines) to the lines vector
		if (!current_line.empty()) {
			lines.push_back(current_line);
			current_line.clear();
		}
	}
	pclose(pipe);

	// Process each line to find headers
	for (const auto &line : lines) {
		std::istringstream iss(line);
		std::vector<std::string> tokens;
		std::string token;
		while (iss >> token) {
			tokens.push_back(token);
		}

		if (tokens.size() < 2) {
			continue; // Skip invalid lines
		}

		// Check if the first prerequisite is the source file
		if (tokens[1] == source_file) {
			for (size_t i = 2; i < tokens.size(); ++i) {
				const std::string &header = tokens[i];
				// Add header if not already present
				if (find(headers.begin(), headers.end(), header) == headers.end()) {
					headers.push_back(header);
				}
			}
		}
	}

	return headers;
}

/*
 * Target
 * ------
 * Used to store build information and to build the target
 */
class Target {
private:
	bool is_built;

public:
	std::string output;
	std::vector<std::string> depends;
	std::string command;

	Target(std::string output, std::vector<std::string> depends, std::string command) {
		this->output = output;
		this->depends = depends;
		this->command = command;
		this->is_built = false;
	}

	/*
	 * Target::create
	 * --------------
	 * Creates a new heap-allocated target
	 */
	static Target *create(std::string output, std::vector<std::string> depends, std::string command) {
		command = rebuild_replace_all(command, "#DEPENDS", rebuild_join(depends, " "));
		command = rebuild_replace_all(command, "#OUT", output);
		return new Target(output, depends, command);
	}

	/*
	 * Target::needs_building
	 * ----------------------
	 * Check if the target should be built
	 */
	bool needs_building() {
		bool modified = false;

		if (is_built)
			return false;
		for (std::string dependency : depends) {
			for (Target *target : rebuild_targets) {
				if (target->output != dependency) {
					continue;
				}
				if (target->needs_building()) {
					return true;
				}
			}
		}

		uint64_t output_date = rebuild_get_modified_date(output);
		for (std::string dependency : depends) {
			// If file modified date is after output's modified data
			if (rebuild_get_modified_date(dependency) >= output_date) {
				modified = true;
				break;
			}
		}

		if (!modified)
			return false;

		return true;
	}

	/*
	 * Target::build
	 * -------------
	 * Builds this target
	 */
	bool build() {
		if (!needs_building())
			return true;

		rebuild_built_targets++;

		int percent;
		percent = rebuild_get_percentage();

		printf("[%3d%%] building: %s\n", percent, output.c_str());

		// Build needed dependencies
		for (std::string dependency : depends) {
			for (Target *target : rebuild_targets) {
				if (target->output == dependency) {
					if (!target->build()) {
						return false;
					}
					break;
				}
			}
			if (!rebuild_fexists(dependency)) {
				return false;
			}
		}

		percent = rebuild_get_percentage();
		printf("[%3d%%] running build cmd for %s\n", percent, output.c_str());

		system(command.c_str());
		is_built = true;

		return true;
	}
};

/*
 * CTarget
 * -------
 * Same as Target, but auto appends needed header files using g++ -MM
 */
class CTarget : public Target {
public:
	/*
	 * CTarget::create
	 * ---------------
	 * Creates a new heap-allocated target, auto appends header files
	 */
	static Target *create(std::string output, std::vector<std::string> depends, std::string command, std::string compiler = REBUILD_STANDARD_CXX_COMPILER, std::string compiler_args = "") {
		command = rebuild_replace_all(command, "#DEPENDS", rebuild_join(depends, " "));
		std::vector<std::string> todo_depends = {};
		for (std::string dependency : depends) {
			if (rebuild_ends_with(dependency, ".c") || rebuild_ends_with(dependency, ".cpp") || rebuild_ends_with(dependency, ".cc")) {
				for (std::string hdr : rebuild_get_included_headers(dependency, compiler, compiler_args)) {
					todo_depends.push_back(hdr);
				}
			} else {
#ifndef REBUILD_NO_WARNINGS
				printf("[    ] warning: %s: headers will not be parsed (non-source "
							 "file)\n",
							 dependency.c_str());
#endif
			}
		}

		for (std::string dependency : todo_depends) {
			depends.push_back(dependency);
#ifdef REBUILD_VERBOSE
			printf("[    ] auto-added dependency: %s\n", dependency.c_str());
#endif
		}
		return Target::create(output, depends, command);
	}
};

/*
 * rebuild_build_targets
 * ---------------------
 * Build all targets once
 */
static bool rebuild_build_targets() {
	for (Target *target : rebuild_targets) {
		if (!target->build()) {
			printf("[ !! ] target %s failed to build\n", target->output.c_str());
			return false;
		}
	}
	return true;
}

/*
 * rebuild_args_shift
 * ------------------
 * Shifts arguments
 */
static std::string rebuild_args_shift(int *argc, char ***argv) {
	assert(*argc > 0 && "argc <= 0");
	--(*argc);
	return *(*argv)++;
}

/*
 * rebuild_update_total_targets
 * ----------------------------
 * Updates total targets
 */
static void rebuild_update_total_targets() {
	rebuild_total_targets = 0;
	for (Target *target : rebuild_targets) {
		if (target->needs_building())
			rebuild_total_targets++;
	}
}

int rebuild_main(int, char **);
int main(int argc, char **argv, char **envp) {
	printf("[    ] rebuild by aceinetx (2022-2025)\n");
	rebuild_path = argv[0];
	rebuild_envp = envp;

	for (unsigned int i = 0; argc; ++i) {
		std::string arg = rebuild_args_shift(&argc, &argv);
		if (arg == "help") {
			printf("[    ] usage: rebuild [option]\n");
			printf("[    ] options:\n");
			printf("[    ]  - help      print help\n");
			printf("[    ]  - build     builds stuff\n");
			printf("[    ]  - clean     cleans targets outputs\n");
			return 0;
		} else if (arg == "build") {
			int err = rebuild_main(argc, argv);
			if (err != 0) {
				printf("[    ] exit with code %d\n", err);
				return err;
			}
			rebuild_update_total_targets();

			// build
			if (rebuild_total_targets != 0) {
				rebuild_build_targets();
			} else {
				printf("[    ] no work to do\n");
			}

			// cleanup
			for (Target *target : rebuild_targets) {
				delete target;
			}

			printf("[100%%] build completed\n");
			return err;
		} else if (arg == "clean") {
			int err = rebuild_main(argc, argv);
			if (err != 0) {
				printf("[    ] exit with code %d\n", err);
				return err;
			}
			for (Target *target : rebuild_targets) {
				try {
					std::filesystem::remove(target->output);
					printf("[    ] removed %s\n", target->output.c_str());
				} catch (...) {
					printf("[    ] failed to remove %s\n", target->output.c_str());
				}
			}
			return err;
		}
	}
	printf("[    ] no options provided. check `rebuild help`\n");
}
#undef main
#define main rebuild_main

#endif
