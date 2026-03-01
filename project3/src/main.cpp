// Copyright 2025 Blaise Tine
//
// Licensed under the Apache License;
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <fstream>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <util.h>
#include "processor.h"
#include "core.h"

using namespace tinyrv;

static void show_usage() {
   std::cout << "Usage: [-s: stats] [-h: help] <program>" << std::endl;
}

bool showStats = false;
const char* program = nullptr;

static void parse_args(int argc, char **argv) {
  int c;
  while ((c = getopt(argc, argv, "sh?")) != -1) {
    switch (c) {
    case 's':
      showStats = true;
      break;
    case 'h':
    case '?':
      show_usage();
      exit(0);
      break;
    default:
      show_usage();
      exit(-1);
    }
  }

  if (optind < argc) {
    program = argv[optind];
    std::cout << "Running " << program << ".." << std::endl;
  } else {
    show_usage();
    exit(-1);
  }
}

int main(int argc, char **argv) {
  int exitcode = -1;

  parse_args(argc, argv);

  {
    // create processor
    Processor processor;

    // load program image into memory
    processor.load_program(program);

    // run simulation
    exitcode = processor.run(true);
    if (exitcode != 0) {
      std::cout << "*** FAILED: exitcode=" << exitcode << std::endl;
    } else {
      std::cout << "PASSED!" << std::endl;
    }

    // show performance stats
    if (showStats) {
      processor.showStats();
    }
  }

  return exitcode;
}
