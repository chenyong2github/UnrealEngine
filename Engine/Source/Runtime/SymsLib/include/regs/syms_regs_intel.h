// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_REGS_INTEL_H
#define SYMS_REGS_INTEL_H

// These FPU structs match the CPU's store layouts exactly.

typedef struct SymsIntelFSave {
  U16 fcw, _pad0;            // control word
  U16 fsw, _pad1;            // status word
  U16 ftw, _pad2;            // tag word
  U32 fip;                   // EIP
  U16 fcs;                   // CS
  U16 fop;                   // last opcode
  U32 fdp;                   // data pointer
  U16 fds, _pad3;            // data selector
  U8 st[80];                 // 8 * fpreg
} SymsIntelFSave;
SYMS_COMPILER_ASSERT(sizeof(SymsIntelFSave) == 108);

typedef struct SymsIntelFXSave {
  U16 fcw;                   // control word
  U16 fsw;                   // status word
  U16 ftw;                   // tag word
  U16 fop;                   // last opcode
  union {
    struct {
      U64 fip;           // RIP
      U64 fdp;           // data pointer
    } format64;
    struct {
      U32 fip;           // EIP
      U16 fcs, _pad0;    // CS
      U32 fdp;           // data pointer
      U16 fds, _pad1;    // data selector
    } format32;
  } u;
  U32 mxcsr;                 // MXCSR Register State
  U32 mxcsr_mask;            // MXCSR Mask
  U8 st[128];                // 8 * (fpreg + padding)
  U8 xmm[256];               // 16 * xmmreg
  U8 reserved[96];
} SymsIntelFXSave;
SYMS_COMPILER_ASSERT(sizeof(SymsIntelFXSave) == 512);

typedef struct SymsIntelXSaveHdr {
  U64 xstate_bv;
  U8 reserved[56];
} SymsIntelXSaveHdr;
SYMS_COMPILER_ASSERT(sizeof(SymsIntelXSaveHdr) == 64);

typedef struct SymsIntelXSave {
  SymsIntelFXSave fxsave;
  SymsIntelXSaveHdr xsave_hdr;

  // NOTE:
  // Technically the layout and size of the XSAVE struct is not defined
  // beyond the header.
  // The idea is that you should query CPUID to find out what features
  // the CPU has, and the size+offset of each one.
  // GDB/LLDB, and even the Linux kernel, assume that the YMMH registers
  // are always at a fixed location. The Intel manuals suggest this to be
  // the case too, but the AMD manuals don't.
  // Anyway. This will do for now, in future we may need to query CPUID
  // ourselves to know the layout here.
  U8 ymmh[256];              // 16 * 16 bytes for each YMMH-reg
} SymsIntelXSave;

struct SymsRegs;

SYMS_INTERNAL void 
syms_regs_x64_fxsave_get_regs(struct SymsRegs *dest, SymsIntelFXSave *src);

SYMS_INTERNAL void 
syms_regs_x64_xsave_get_regs(struct SymsRegs *dest, SymsIntelXSave *src);

SYMS_INTERNAL void 
syms_regs_x86_fxsave_get_regs(struct SymsRegs *dest, SymsIntelFXSave *src);

SYMS_INTERNAL void 
syms_regs_x86_xsave_get_regs(struct SymsRegs *dest, SymsIntelXSave *src);

SYMS_INTERNAL void 
syms_regs_x64_fxsave_put_regs(SymsIntelFXSave *dest, struct SymsRegs *src);

SYMS_INTERNAL void 
syms_regs_x64_xsave_put_regs(SymsIntelXSave *dest, struct SymsRegs *src);

SYMS_INTERNAL void 
syms_regs_x86_fxsave_put_regs(SymsIntelFXSave *dest, struct SymsRegs *src);

SYMS_INTERNAL void 
syms_regs_x86_xsave_put_regs(SymsIntelXSave *dest, struct SymsRegs *src);

#endif /* SYMS_REGS_INTEL_H */
