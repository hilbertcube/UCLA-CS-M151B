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

#include <string>
#include <cstring>
#include <util.h>
#include "memory.h"

using namespace tinyrv;

Memory::Memory(const SimContext& ctx, const char* name)
  : SimObject<Memory>(ctx, name)
  , imem_req(this)
  , imem_rsp(this)
  , dmem_req(this)
  , dmem_rsp(this)
  , ram_(RAM_PAGE_SIZE) {
  this->reset();
}

Memory::~Memory() {
  // --
}

void Memory::reset() {
  // Drain pending inbound requests.
  while (!imem_req.empty()) {
    imem_req.pop();
  }
  while (!dmem_req.empty()) {
    dmem_req.pop();
  }
}

void Memory::tick() {
  auto service_port = [this](SimChannel<mem_req_t>& req_ch, SimChannel<mem_rsp_t>& rsp_ch) {
    if (req_ch.empty())
      return;
    auto req = req_ch.peek();
    if (req.is_write) {
      for (uint32_t i = 0; i < MEM_BLOCK_SIZE; ++i) {
        if (i < 64 && (req.byteen & (1ull << i))) {
          uint8_t value = req.data ? (*(req.data))[i] : 0;
          ram_.write(&value, req.addr + i, 1);
        }
      }
      req_ch.pop();
      return;
    } else {
      if (rsp_ch.full())
        return;
      mem_rsp_t rsp{std::make_shared<mem_block_t>()};
      std::memset(rsp.data->data(), 0, rsp.data->size());
      ram_.read(rsp.data->data(), req.addr, rsp.data->size());
      rsp_ch.send(rsp, 0);
      req_ch.pop();
    }
  };

  service_port(imem_req, imem_rsp);
  service_port(dmem_req, dmem_rsp);
}

void Memory::load_program(const char* program) {
  std::string ext(fileExtension(program));
  if (ext == "bin") {
    ram_.loadBinImage(program, STARTUP_ADDR);
  } else if (ext == "hex") {
    ram_.loadHexImage(program);
  } else {
    std::cout << "*** error: only *.bin or *.hex images supported." << std::endl;
    std::abort();
  }
}
