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

#include "processor.h"
#include "processor_impl.h"

using namespace tinyrv;

ProcessorImpl::ProcessorImpl() {
  // initialize simulator
  SimPlatform::instance().initialize();

  // create backing memory
  memory_ = Memory::Create("memory");

  // create the core
  core_ = Core::Create(0, this);

  // connect core instruction and data caches with memory
  auto icache = core_->get_icache();
  auto dcache = core_->get_dcache();

  icache->mem_req.bind(&memory_->imem_req);
  memory_->imem_rsp.bind(&icache->mem_rsp);

  dcache->mem_req.bind(&memory_->dmem_req);
  memory_->dmem_rsp.bind(&dcache->mem_rsp);

  this->reset();
}

ProcessorImpl::~ProcessorImpl() {
  // Terminate simulator
  SimPlatform::instance().finalize();
}

void ProcessorImpl::reset() {
  core_->reset();
}

void ProcessorImpl::load_program(const char* program) {
  memory_->load_program(program);
}

int ProcessorImpl::run(bool riscv_test) {
  SimPlatform::instance().reset();
  this->reset();

  bool done;
  Word exitcode = 0;
  do {
    SimPlatform::instance().tick();
    done = core_->check_exit(&exitcode, riscv_test);
  } while (!done);

  return exitcode;
}

void ProcessorImpl::showStats() {
  core_->showStats();
}

///////////////////////////////////////////////////////////////////////////////

Processor::Processor()
  : impl_(new ProcessorImpl())
{}

Processor::~Processor() {
  delete impl_;
}

void Processor::load_program(const char* program) {
  impl_->load_program(program);
}

int Processor::run(bool riscv_test) {
  return impl_->run(riscv_test);
}

void Processor::showStats() {
  impl_->showStats();
}