/*
 * rebuild
 * Copyright (c) aceinet
 * License: GPL-2.0
 */
#pragma once

#ifndef __cplusplus
#error "rebuild requires c++"
#else

#include <chrono>
#include <filesystem>
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
class Target;

char *rebuild_path;
char **rebuild_envp;
std::vector<Target *> rebuild_targets;
uint64_t last_run_time = 0;

bool rebuild_fexists(std::string &name) {
  return (access(name.c_str(), F_OK) != -1);
}

uint64_t rebuild_get_modified_date(std::string &filename) {
  namespace fs = std::filesystem;

  try {
    auto ftime = fs::last_write_time(filename);
    auto sctp =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() +
            std::chrono::system_clock::now());
    auto unixTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
                             sctp.time_since_epoch())
                             .count();

    return static_cast<uint64_t>(unixTimestamp);
  } catch (const fs::filesystem_error &e) {
    return 0;
  }
}

class Target {
private:
  bool is_built;

public:
  std::string output;
  std::vector<std::string> depends;
  std::string command;

  Target(std::string output, std::vector<std::string> depends,
         std::string command) {
    this->output = output;
    this->depends = depends;
    this->command = command;
    this->is_built = false;
  }
  ~Target() {}

  bool build() {
    bool modified = false;
    uint64_t output_date = rebuild_get_modified_date(output);
    for (std::string dependency : depends) {
      if (rebuild_get_modified_date(dependency) >= output_date) {
        modified = true;
        break;
      }
    }

    if (is_built || !modified)
      return true;
    printf("[rebuild] building: %s\n", output.c_str());
    for (std::string dependency : depends) {
      if (!rebuild_fexists(dependency)) {
        bool built = false;
        for (Target *target : rebuild_targets) {
          if (target->output == dependency) {
            printf("[rebuild] building dependency: %s\n",
                   target->output.c_str());
            if (!target->build()) {
              return false;
            }
            built = true;
          }
        }
        if (!built)
          return false;
      }
    }

    system(command.c_str());
    is_built = true;

    return true;
  }
};

bool build_targets() {
  for (Target *target : rebuild_targets) {
    if (!target->build()) {
      printf("[rebuild] target %s failed to build (maybe non-existent "
             "dependency)\n",
             target->output.c_str());
      return false;
    }
  }
  return true;
}

int rebuild_main(int, char **);
int main(int argc, char **argv, char **envp) {
  printf("[rebuild] building\n");
  rebuild_path = argv[0];
  rebuild_envp = envp;

  int err = rebuild_main(argc, argv);
  // build
  if (build_targets())
    build_targets();
  // cleanup
  for (Target *target : rebuild_targets) {
    delete target;
  }
  return err;
}
#define main rebuild_main

#endif
