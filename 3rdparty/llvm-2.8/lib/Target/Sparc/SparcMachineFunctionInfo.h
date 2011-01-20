//===- SparcMachineFunctionInfo.h - Sparc Machine Function Info -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares  Sparc specific per-machine-function information.
//
//===----------------------------------------------------------------------===//
#ifndef SPARCMACHINEFUNCTIONINFO_H
#define SPARCMACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

  class SparcMachineFunctionInfo : public MachineFunctionInfo {
  private:
    unsigned GlobalBaseReg;

    /// VarArgsFrameOffset - Frame offset to start of varargs area.
    int VarArgsFrameOffset;

  public:
    SparcMachineFunctionInfo() : GlobalBaseReg(0), VarArgsFrameOffset(0) {}
    explicit SparcMachineFunctionInfo(MachineFunction &MF)
      : GlobalBaseReg(0), VarArgsFrameOffset(0) {}

    unsigned getGlobalBaseReg() const { return GlobalBaseReg; }
    void setGlobalBaseReg(unsigned Reg) { GlobalBaseReg = Reg; }

    int getVarArgsFrameOffset() const { return VarArgsFrameOffset; }
    void setVarArgsFrameOffset(int Offset) { VarArgsFrameOffset = Offset; }
  };
}

#endif