// Copyright 2024 Blaise Tine
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

#include <simobject.h>
#include "instr.h"
#include "types.h"

namespace tinyrv {

class CDBResult {
public:
  CDBResult(uint32_t res = 0, uint32_t rob = 0)
    : value(res)
    , rob_tag(rob)
  {}

  uint32_t value;    // computed value
  uint32_t rob_tag; // ROB entry
};

class CommonDataBus {
public:

  typedef CDBResult data_t;  // For backward compatibility

  CommonDataBus() : empty_(true) {}

  ~CommonDataBus() {}

  bool empty() const {
    return empty_;
  }

  const data_t& result() const {
    assert(!empty_);
    return result_;
  }

  void push(const CDBResult& result) {
    assert(empty_);
    result_ = result;
    empty_ = false;
  }

  void pop() {
    assert(!empty_);
    empty_ = true;
  }

  void invalidate(uint32_t rob_tag) {
    if (!empty_ && is_rob_younger(result_.rob_tag, rob_tag)) {
      empty_ = true;
    }
  }

private:
  bool      empty_;
  CDBResult result_;
};

}