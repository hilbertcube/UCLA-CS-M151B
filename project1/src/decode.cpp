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
#include <string>
#include <stdlib.h>
#include <string.h>
#include <iomanip>
#include <vector>
#include <unordered_map>
#include <util.h>
#include "debug.h"
#include "types.h"
#include "core.h"
#include "instr.h"

using namespace tinyrv;

namespace tinyrv {

// opcode -> instruction type
static const std::unordered_map<Opcode, InstType> sc_instTable = {
  {Opcode::R,     InstType::R},
  {Opcode::L,     InstType::I},
  {Opcode::I,     InstType::I},
  {Opcode::S,     InstType::S},
  {Opcode::B,     InstType::B},
  {Opcode::LUI,   InstType::U},
  {Opcode::AUIPC, InstType::U},
  {Opcode::JAL,   InstType::J},
  {Opcode::JALR,  InstType::I},
  {Opcode::SYS,   InstType::I},
  {Opcode::FENCE, InstType::I},
};

enum Constants {
  width_opcode= 7,
  width_reg   = 5,
  width_func3 = 3,
  width_func7 = 7,
  width_i_imm = 12,
  width_j_imm = 20,

  shift_opcode= 0,
  shift_rd    = width_opcode,
  shift_func3 = shift_rd + width_reg,
  shift_rs1   = shift_func3 + width_func3,
  shift_rs2   = shift_rs1 + width_reg,
  shift_func2 = shift_rs2 + width_reg,
  shift_func7 = shift_rs2 + width_reg,

  mask_opcode = (1 << width_opcode)- 1,
  mask_reg    = (1 << width_reg)   - 1,
  mask_func3  = (1 << width_func3) - 1,
  mask_func7  = (1 << width_func7) - 1,
  mask_i_imm  = (1 << width_i_imm) - 1,
  mask_j_imm  = (1 << width_j_imm) - 1,
};

static const char* op_string(const Instr &instr) {
  auto opcode = instr.getOpcode();
  auto func3  = instr.getFunc3();
  auto func7  = instr.getFunc7();
  auto imm    = instr.getImm();

  switch (opcode) {
  case Opcode::LUI:   return "LUI";
  case Opcode::AUIPC: return "AUIPC";
  case Opcode::R:
    switch (func3) {
    case 0:
        switch (func7) {
        case 0x00:
            return "ADD";
        case 0x20:
            return "SUB";
        case 0x01:
            return "MUL";
        }
        std::abort();
    case 1: return (func7 & 0x1) ? "MULH" : "SLL";
    case 2: return (func7 & 0x1) ? "MULHSU" : "SLT";
    case 3: return (func7 & 0x1) ? "MULHU" : "SLTU";
    case 4: return "XOR";
    case 5: return (func7 & 0x20) ? "SRA" : "SRL";
    case 6: return "OR";
    case 7: return "AND";
    default:
      std::abort();
    }
  case Opcode::I:
    switch (func3) {
    case 0: return "ADDI";
    case 1: return "SLLI";
    case 2: return "SLTI";
    case 3: return "SLTIU";
    case 4: return "XORI";
    case 5: return (func7 & 0x20) ? "SRAI" : "SRLI";
    case 6: return "ORI";
    case 7: return "ANDI";
    default:
      std::abort();
    }
  case Opcode::B:
    switch (func3) {
    case 0: return "BEQ";
    case 1: return "BNE";
    case 4: return "BLT";
    case 5: return "BGE";
    case 6: return "BLTU";
    case 7: return "BGEU";
    default:
      std::abort();
    }
  case Opcode::JAL:  return "JAL";
  case Opcode::JALR: return "JALR";
  case Opcode::L:
    switch (func3) {
    case 0: return "LB";
    case 1: return "LH";
    case 2: return "LW";
    case 3: return "LD";
    case 4: return "LBU";
    case 5: return "LHU";
    case 6: return "LWU";
    default:
      std::abort();
    }
  case Opcode::S:
    switch (func3) {
    case 0: return "SB";
    case 1: return "SH";
    case 2: return "SW";
    case 3: return "SD";
    default:
      std::abort();
    }
  case Opcode::SYS:
    switch (func3) {
    case 0:
      switch (imm) {
      case 0x000: return "ECALL";
      case 0x001: return "EBREAK";
      case 0x002: return "URET";
      case 0x102: return "SRET";
      case 0x302: return "MRET";
      default:
        std::abort();
      }
    case 1: return "CSRRW";
    case 2: return "CSRRS";
    case 3: return "CSRRC";
    case 5: return "CSRRWI";
    case 6: return "CSRRSI";
    case 7: return "CSRRCI";
    default:
      std::abort();
    }
  case Opcode::FENCE:
    return "FENCE";
  default:
    std::abort();
  }
}

std::ostream &operator<<(std::ostream &os, const Instr &instr) {
  os << op_string(instr);
  int sep = 0;

  auto exec_flags = instr.getExeFlags();

  if (exec_flags.use_rd) {
    if (sep++ != 0) { os << ", "; } else { os << " "; }
    os << "x" << std::dec << instr.getRd();
  }

  if (exec_flags.use_rs1) {
    if (sep++ != 0) { os << ", "; } else { os << " "; }
    os << "x" << std::dec << instr.getRs1();
  }

  if (exec_flags.use_rs2) {
    if (sep++ != 0) { os << ", "; } else { os << " "; }
    os << "x" << std::dec << instr.getRs2();
  }

  if (exec_flags.use_imm) {
    if (sep++ != 0) { os << ", "; } else { os << " "; }
    os << "0x" << std::hex << instr.getImm();
  }

  os << std::dec << ", alu_op=" << instr.getAluOp()
      << ", br_op=" << instr.getBrOp()
     << ", exe_flags=" << instr.getExeFlags();

  return os;
}

}

std::shared_ptr<Instr> Core::decode(uint32_t instr_code) const {
  auto instr = std::make_shared<Instr>();
  auto opcode = Opcode((instr_code >> shift_opcode) & mask_opcode);

  auto func3 = (instr_code >> shift_func3) & mask_func3;
  auto func7 = (instr_code >> shift_func7) & mask_func7;

  auto rd  = (instr_code >> shift_rd)  & mask_reg;
  auto rs1 = (instr_code >> shift_rs1) & mask_reg;
  auto rs2 = (instr_code >> shift_rs2) & mask_reg;

  auto op_it = sc_instTable.find(opcode);
  if (op_it == sc_instTable.end()) {
    std::cout << std::hex << "Error: invalid opcode: 0x" << static_cast<int>(opcode) << std::endl;
    return nullptr;
  }

  ExeFlags exe_flags;
  memset(&exe_flags, 0, sizeof(ExeFlags));
  uint32_t imm = 0x0;

  // instruction type decoding

  auto inst_type = op_it->second;
  switch (inst_type) {
  case InstType::R:
    exe_flags.use_rd  = 1;
    exe_flags.use_rs1 = 1;
    exe_flags.use_rs2 = 1;
    break;

  case InstType::I: {
    switch (opcode) {
    case Opcode::I: // I-type instructions
      exe_flags.use_rd  = 1;
      exe_flags.use_rs1 = 1;
      exe_flags.use_imm = 1;
      exe_flags.alu_s2_imm = 1;
      // I-type immediate for ALU immediates: bits 31-20, sign-extended
      imm = sext(instr_code >> 20, 12);
      break;
    case Opcode::L:
    case Opcode::JALR: {
      exe_flags.use_rd  = 1;
      exe_flags.use_rs1 = 1;
      exe_flags.use_imm = 1;
      exe_flags.alu_s2_imm = 1;

      // I-type immediate for loads and JALR: bits 31-20, sign-extended
      imm = sext(instr_code >> 20, 12);
    } break;
    case Opcode::SYS: {
      exe_flags.use_imm = 1;
      if (func3 != 0) {
        // CSR instructions
        exe_flags.use_rd = 1;
        if (func3 < 5) {
          exe_flags.use_rs1 = 1;
        }
      }
      imm = 0;
    } break;
    case Opcode::FENCE:
      break;
    default:
      std::abort();
      break;
    }
  } break;
  case InstType::S: {
    exe_flags.use_rs1 = 1;
    exe_flags.use_rs2 = 1;
    exe_flags.use_imm = 1;
    exe_flags.alu_s2_imm = 1;
    

    // fixed s type immediate
    uint32_t imm_11_5 = (instr_code >> 25) & 0x7F;
    uint32_t imm_4_0  = (instr_code >> 7)  & 0x1F;
    uint32_t imm_raw  = (imm_11_5 << 5) | imm_4_0;
    imm = sext(imm_raw, 12);
  } break;

  case InstType::B: {
    exe_flags.use_rs1 = 1;
    exe_flags.use_rs2 = 1;
    exe_flags.use_imm = 1;
    exe_flags.alu_s2_imm = 1;

    // B-type immediate: imm[12|10:5] in bits 31-25, imm[4:1|11] in bits 11-7
    uint32_t imm_12 = (instr_code >> 31) & 0x1;     // bit 31 -> im[12]
    uint32_t imm_10_5 = (instr_code >> 25) & 0x3F;  // bits 30-25 (6 bits) -> imm[10:5]
    uint32_t imm_4_1 = (instr_code >> 8) & 0xF;     // bits 11-8 (4 bits) -> imm[4:1]
    uint32_t imm_11 = (instr_code >> 7) & 0x1;      // bit 7 -> imm[11]

    uint32_t imm_raw = (imm_12 << 12) | (imm_11 << 11) | (imm_10_5 << 5) | (imm_4_1 << 1);

    // sign extend from bit 12
    imm = sext(imm_raw, 13);
  } break;

  case InstType::U: {
    exe_flags.use_rd  = 1;
    exe_flags.use_imm = 1;
    exe_flags.alu_s2_imm = 1;

    uint32_t imm_31_12 = (instr_code >> 12) & 0xFFFFF;
    imm = imm_31_12 << 12;
  } break;

  case InstType::J: {
    exe_flags.use_rd  = 1;
    exe_flags.use_imm = 1;
    exe_flags.alu_s2_imm = 1;
    // J-type immediate: imm[20|10:1|11|19:12] in bits 31-12
    // Layout in instruction:
    //   bit 31      -> imm[20]
    //   bits 30-21  -> imm[10:1]
    //   bit 20      -> imm[11]
    //   bits 19-12  -> imm[19:12]
    //   imm[0] is implicitly 0 (word aligned -- technically 2 byte alligned actually), so we shift left by 1.
    uint32_t imm_20    = (instr_code >> 31) & 0x1;
    uint32_t imm_10_1  = (instr_code >> 21) & 0x3FF;
    uint32_t imm_11    = (instr_code >> 20) & 0x1;
    uint32_t imm_19_12 = (instr_code >> 12) & 0xFF;

    uint32_t imm_raw =
        (imm_20 << 20) |
        (imm_19_12 << 12) |
        (imm_11 << 11) |
        (imm_10_1 << 1);

    // Sign-extend from bit 20 (total width 21 bits) in case of negative value
    imm = sext(imm_raw, 21);
  } break;

  default:
    std::abort();
  }

  // instruction opcode decoding

  AluOp alu_op = AluOp::NONE;
  BrOp br_op = BrOp::NONE;

  switch (opcode) {
  case Opcode::LUI: {
    // RV32I: LUI
    // LUI writes the upper immediate directly into rd
    //0 + imm.
    alu_op = AluOp::ADD;
    break;
  }
  case Opcode::AUIPC: {
    // RV32I: AUIPC
    // AUIPC computes rd = PC + imm, so we use ADD with s1=PC and s2=imm.
    // rd = PC + (imm << 12)
    alu_op = AluOp::ADD;
    exe_flags.alu_s1_PC = 1; // PC is the source1 for ADD
    break;
  }
  case Opcode::R: {
    // R-type ALU operations: decode based on func3 and func7
    switch (func3) {
    case 0:
      switch (func7) {
      case 0x00:
        alu_op = AluOp::ADD;
        break;
      case 0x20:
        alu_op = AluOp::SUB;
        break;
      case 0x01:
        alu_op = AluOp::MUL;
        break;
      default:
        std::abort();
      }
      break;
    case 1:
      alu_op = (func7 == 0x1) ? AluOp::MULH : AluOp::SLL;
      break;
    case 2:
      alu_op = (func7 == 0x1) ? AluOp::MULHSU : AluOp::LTI;
      break;
    case 3:
      alu_op = (func7 == 0x1) ? AluOp::MULHU : AluOp::LTU;
      break;
    case 4:
      alu_op = AluOp::XOR;
      break;
    case 5:
      alu_op = (func7 == 0x20) ? AluOp::SRA : AluOp::SRL;
      break;
    case 6:
      alu_op = AluOp::OR;
      break;
    case 7:
      alu_op = AluOp::AND;
      break;
    default:
      std::abort();
    }
    break;
  }
  case Opcode::I: {
    switch (func3){
      case 0:
        alu_op = AluOp::ADD;
        break;
      case 1:
        alu_op = AluOp::SLL;
        break;
      case 2:
        alu_op = AluOp::LTI;
        break;
      case 3:
        alu_op = AluOp::LTU;
        break;
      case 4:
        alu_op = AluOp::XOR;
        break;
      case 5:
        alu_op = (func7 == 0x20) ? AluOp::SRA : AluOp::SRL;
        break;
      case 6:
        alu_op = AluOp::OR;
        break;
      case 7:
        alu_op = AluOp::AND;
        break;
      default:
        std::abort();
        break;
    }
    break;
  }
  case Opcode::B: {
    exe_flags.alu_s1_PC = 1; // PC + imm 
    alu_op = AluOp::ADD; // because we are adding PC and imm
    switch (func3) {
    case 0: br_op = BrOp::BEQ;  break;
    case 1: br_op = BrOp::BNE;  break;
    case 4: br_op = BrOp::BLT;  break;
    case 5: br_op = BrOp::BGE;  break;
    case 6: br_op = BrOp::BLTU; break;
    case 7: br_op = BrOp::BGEU; break;
    default:
      std::abort();
    }
    break;
  }
  case Opcode::JAL: {
    exe_flags.alu_s1_PC = 1;
    alu_op = AluOp::ADD;
    br_op = BrOp::JAL;
    break;
  }
  case Opcode::JALR: {
    // JALR: jump to( rs1 + imm) & ~ 1 for 2 byte alignment and writes PC+4 to rd
    alu_op = AluOp::ADD;
    br_op = BrOp::JALR;
    break;
  }
  case Opcode::L: {
    // RV32I: LB, LH, LW, LBU, LHU
    // effective addy = rs1 + imm
    alu_op = AluOp::ADD;
    exe_flags.is_load = 1;
    break;
  }
  case Opcode::S: {
    // RV32I: SB, SH, SW 
    // effective addy = rs1 + imm
    alu_op = AluOp::ADD;
    exe_flags.is_store = 1;
    break;
  }
  case Opcode::SYS: {
    if (func3 == 0) {
      alu_op = AluOp::ADD;
      switch (imm) {
      case 0x000: // RV32I: ECALL
      case 0x001: // RV32I: EBREAK
        exe_flags.is_exit = 1;
        break;
      case 0x002: // RV32I: URET
      case 0x102: // RV32I: SRET
      case 0x302: // RV32I: MRET
        break;
      default:
        std::abort();
      }
    } else {
      exe_flags.is_csr = 1;
      exe_flags.alu_s2_csr = 1;
      switch (func3) {
      case 1: {
        // RV32I: CSRRW
        alu_op = AluOp::ADD;
        break;
      }
      case 2: {
        // RV32I: CSRRS
        alu_op = AluOp::OR;
        break;
      }
      case 3: {
        // RV32I: CSRRC
        alu_op = AluOp::AND;
        exe_flags.alu_s1_inv = 1;
        break;
      }
      case 5: {
        // RV32I: CSRRWI
        alu_op = AluOp::ADD;
        exe_flags.alu_s1_rs1 = 1;
        break;
      }
      case 6: {
        // RV32I: CSRRSI;
        alu_op = AluOp::OR;
        exe_flags.alu_s1_rs1 = 1;
        break;
      }
      case 7: {
        // RV32I: CSRRCI
        alu_op = AluOp::AND;
        exe_flags.alu_s1_inv = 1;
        exe_flags.alu_s1_rs1 = 1;
        break;
      }
      default:
        std::abort();
      }
    }
    break;
  }
  case Opcode::FENCE: {
    // RV32I: FENCE
    break;
  }
  default:
    std::abort();
  }

  instr->setOpcode(opcode);
  instr->setRd(rd);
  instr->setSrc1(rs1);
  instr->setSrc2(rs2);
  instr->setImm(imm);
  instr->setFunc3(func3);
  instr->setFunc7(func7);
  instr->setAluOp(alu_op);
  instr->setBrOp(br_op);
  instr->setExeFlags(exe_flags);

  return instr;
}
