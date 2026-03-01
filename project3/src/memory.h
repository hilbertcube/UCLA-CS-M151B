// Copyright 2026 Blaise Tine
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

#pragma once

#include <array>
#include <memory>
#include <simobject.h>
#include <mem.h>
#include "config.h"

namespace tinyrv {

using mem_block_t = std::array<uint8_t, MEM_BLOCK_SIZE>;

struct mem_req_t {
  bool is_write;
  uint64_t addr;
  uint64_t byteen;
  std::shared_ptr<mem_block_t> data;
};

struct mem_rsp_t {
  std::shared_ptr<mem_block_t> data;
};

class Memory : public SimObject<Memory> {
public:
  SimChannel<mem_req_t> imem_req;
  SimChannel<mem_rsp_t> imem_rsp;
  SimChannel<mem_req_t> dmem_req;
  SimChannel<mem_rsp_t> dmem_rsp;

  Memory(const SimContext& ctx, const char* name);
  ~Memory();

  void reset();
  void tick();

  void load_program(const char* program);

private:
  RAM ram_;
};

} // namespace tinyrv
