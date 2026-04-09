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

#include <cassert>
#include <unordered_map>
#include <simobject.h>
#include "instr.h"
#include "CDB.h"
#include "types.h"

namespace tinyrv {

class Core;

uint32_t execute_alu_op(const Instr &instr, uint32_t rs1_data, uint32_t rs2_data);

class FunctionalUnit : public SimObject<FunctionalUnit> {
public:
  typedef std::shared_ptr<FunctionalUnit> Ptr;

  FunctionalUnit(const SimContext& ctx, const std::string& name, Core* core)
    : SimObject<FunctionalUnit>(ctx, name)
    , core_(core)
  {}

  virtual ~FunctionalUnit() {}

  bool full() const {
    return input_.valid;
  }

  bool empty() const {
    return !output_.valid;
  }

  void push(Instr::Ptr instr, uint32_t rob_tag, uint32_t rs1_value, uint32_t rs2_value) {
    input_.valid     = true;
    input_.instr     = instr;
    input_.rob_tag   = rob_tag;
    input_.rs1_value = rs1_value;
    input_.rs2_value = rs2_value;
    cycles_          = 0;
  }

  CDBResult pop() {
    output_.valid = false;
    return output_.result;
  }

  virtual void tick() = 0;

  virtual void reset() {
    output_.valid = false;
    output_.result = false;
  }

  virtual void invalidate(uint32_t rob_tag) {
    if (input_.valid && is_rob_younger(input_.rob_tag, rob_tag)) {
      input_.valid   = false;
    }
    if (output_.valid && is_rob_younger(output_.result.rob_tag, rob_tag)) {
      output_.valid = false;
    }
  }

protected:

  struct input_t {
    bool     valid;
    Instr::Ptr instr;
    uint32_t rob_tag;
    uint32_t rs1_value;
    uint32_t rs2_value;
  };

  struct output_t {
    bool    valid;
    CDBResult result;
  };

  Core*    core_;
  input_t  input_;
  output_t output_;
  uint32_t cycles_;
};

///////////////////////////////////////////////////////////////////////////////

class ALU : public FunctionalUnit {
public:
  using Ptr = std::shared_ptr<ALU>;

  ALU(const SimContext& ctx, const std::string& name, Core* core)
    : FunctionalUnit(ctx, name, core)
  {}

  void tick() override;
};

///////////////////////////////////////////////////////////////////////////////

class BRU : public FunctionalUnit {
public:
  using Ptr = std::shared_ptr<BRU>;

  BRU(const SimContext& ctx, const std::string& name, Core* core)
    : FunctionalUnit(ctx, name, core)
  {}

  void tick() override;
};

///////////////////////////////////////////////////////////////////////////////

class LSU : public FunctionalUnit {
public:
  using Ptr = std::shared_ptr<LSU>;

  LSU(const SimContext& ctx, const std::string& name, Core* core)
    : FunctionalUnit(ctx, name, core)
  {}

  void tick() override;

  bool issue_load(uint32_t addr, uint32_t fwd_data, uint32_t lsq_idx, bool forwarded);

  bool issue_store(uint32_t addr, uint32_t data, uint32_t lsq_idx);
};

///////////////////////////////////////////////////////////////////////////////

class SFU : public FunctionalUnit {
public:
  using Ptr = std::shared_ptr<SFU>;

  SFU(const SimContext& ctx, const std::string& name, Core* core)
    : FunctionalUnit(ctx, name, core)
  {}

  void tick() override;
};

}
