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

#include <iostream>
#include <assert.h>
#include <util.h>
#include "FU.h"
#include "core.h"

using namespace tinyrv;

void SFU::tick() {
  if (!input_.valid)
    return; // no instruction to execute
  if (++cycles_ < SFU_LATENCY)
    return; // wait until operation is complete
  if (output_.valid)
    return; // wait until output is consumed

  auto csr_data = core_->get_csr(input_.instr->getImm());
  auto rd_data = execute_alu_op(*input_.instr, input_.rs1_value, csr_data);
  if (rd_data != csr_data) {
    core_->set_csr(input_.instr->getImm(), rd_data);
  }

  // send result
  output_.result = CDBResult(csr_data, input_.rob_tag);
  output_.valid = true;

  // consume input
  input_.valid = false;
}