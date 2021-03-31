// Copyright Epic Games, Inc. All Rights Reserved.
// Constructs an x87 tag value given a 10-byte float.
SYMS_INTERNAL U32 
syms_regs_make_tag(const void *fp10)
{
  U16 exponent = ((const U16 *)fp10)[4] & 0x7fff;
  U64 bits = ((const U64 *)fp10)[0];

  U64 j = bits >> 63;
  U64 frac = bits & ~((U64)1 << 63);

  if (exponent == 0x7fff) {
    return 2; // special
  } else if (exponent == 0) {
    return (frac == 0 && !j) ? /* zero */ 1 : /* special */ 2;
  } else {
    return j ? /* valid */ 0 : /*special */ 2;
  }
}

SYMS_INTERNAL U16 
syms_regs_fix_tag_word(SymsIntelFXSave *fpu)
{
  // The FXSAVE/XSAVE instructions do not save out the correct FPU tag word.
  // But, we can recreate the correct value by looking at the actual FP data.
  // (see FXSAVE instruction in Intel IA32 Software Developers Manual)

  U16 realFtw = 0;
  U32 top = (fpu->fsw >> 11) & 7;
  u32 fpr;
  for (fpr = 0; fpr < 8; fpr++) {
    U32 tag;

    if (fpu->ftw & (1 << fpr)) {
      U32 st = (fpr - top) & 7;
      tag = syms_regs_make_tag((const void *)&fpu->st[st*16]);
    } else {
      tag = 3; // empty
    }

    realFtw |= tag << (2 * fpr);
  }

  return realFtw;
}

SYMS_INTERNAL U16 
syms_regs_break_tag_word(U16 ftw)
{
  // Generates the compact tag word used by FXSAVE/XSAVE.
  U16 compact = 0;
  U32 fpr;
  for (fpr = 0; fpr < 8; fpr++) {
    U32 tag = (ftw >> (fpr * 2)) & 3;
    if (tag != 3) {
      compact |= (1 << fpr);
    }
  }
  return compact;
}

// X64 --------------------------------------------------------------------

SYMS_COMPILER_ASSERT(SYMS_REG_X64_st7 == SYMS_REG_X64_st0 + 7);
SYMS_COMPILER_ASSERT(SYMS_REG_X86_xmm7 == SYMS_REG_X86_xmm0 + 7);
SYMS_COMPILER_ASSERT(SYMS_REG_X64_xmm15 == SYMS_REG_X64_xmm0 + 15);
SYMS_COMPILER_ASSERT(SYMS_REG_X64_ymm15 == SYMS_REG_X64_ymm0 + 15);
SYMS_COMPILER_ASSERT(SYMS_REG_X64_ymm15 == SYMS_REG_X64_ymm0 + 15);

SYMS_INTERNAL void 
syms_regs_x64_fxsave_get_regs(SymsRegs *dest, SymsIntelFXSave *src)
{
  u32 n;
  U16 ftw = syms_regs_fix_tag_word(src);
  syms_regs_set_value(dest, SYMS_REG_X64_fcw, &src->fcw, sizeof(src->fcw));
  syms_regs_set_value(dest, SYMS_REG_X64_fsw, &src->fsw, sizeof(src->fsw));
  syms_regs_set_value(dest, SYMS_REG_X64_ftw, &ftw, sizeof(src->ftw));
  syms_regs_set_value(dest, SYMS_REG_X64_fop, &src->fop, sizeof(src->fop));
  syms_regs_set_value(dest, SYMS_REG_X64_fip, &src->u.format64.fip, sizeof(src->u.format64.fip));
  syms_regs_set_value(dest, SYMS_REG_X64_fdp, &src->u.format64.fdp, sizeof(src->u.format64.fdp));
  syms_regs_set_value(dest, SYMS_REG_X64_mxcsr, &src->mxcsr, sizeof(src->mxcsr));
  for (n = 0; n < 8; n++) {
    syms_regs_set_value(dest, (SymsRegID)(SYMS_REG_X64_st0 + n), &src->st[n*16], sizeof(src->st[n*16]));
  }
  for (n = 0; n < 16; n++) {
    syms_regs_set_value(dest, (SymsRegID)(SYMS_REG_X64_xmm0 + n), &src->xmm[n*16], sizeof(src->xmm[n*16]));
  }
}

SYMS_INTERNAL void 
syms_regs_x64_fxsave_put_regs(SymsIntelFXSave *dest, struct SymsRegs *src)
{
  U32 n;
  syms_regs_get_value(src, SYMS_REG_X64_fcw, &dest->fcw, sizeof(dest->fcw));
  syms_regs_get_value(src, SYMS_REG_X64_fsw, &dest->fsw, sizeof(dest->fsw));
  syms_regs_get_value(src, SYMS_REG_X64_ftw, &dest->ftw, sizeof(dest->ftw));
  syms_regs_get_value(src, SYMS_REG_X64_fop, &dest->fop, sizeof(dest->fop));
  syms_regs_get_value(src, SYMS_REG_X64_fip, &dest->u.format64.fip, sizeof(dest->u.format64.fip));
  syms_regs_get_value(src, SYMS_REG_X64_fdp, &dest->u.format64.fdp, sizeof(dest->u.format64.fdp));
  syms_regs_get_value(src, SYMS_REG_X64_mxcsr, &dest->mxcsr, sizeof(dest->mxcsr));
  for (n = 0; n < 8; n++) {
    syms_regs_get_value(src, (SymsRegID)(SYMS_REG_X64_st0+n), &dest->st[n*16], sizeof(dest->st[0]));
  }
  for (n = 0; n < 16; n++) {
    syms_regs_get_value(src, (SymsRegID)(SYMS_REG_X64_xmm0+n), &dest->xmm[n*16], sizeof(dest->xmm[0]));
  }
  dest->ftw = syms_regs_break_tag_word(dest->ftw);
}

SYMS_INTERNAL void 
syms_regs_x64_xsave_get_regs(SymsRegs *dest, SymsIntelXSave *src)
{
  syms_regs_x64_fxsave_get_regs(dest, &src->fxsave);

  // Check if YMM regs exist
  if (src->xsave_hdr.xstate_bv & 4) {
    U8 reg[32];
    U32 n;
    for (n = 0; n < 16; n++) {
      syms_memcpy(&reg[0], &src->fxsave.xmm[n*16], 16); // construct the two halves
      syms_memcpy(&reg[16], &src->ymmh[n*16], 16);
      syms_regs_set_value(dest, (SymsRegID)(SYMS_REG_X64_ymm0 + n), reg, sizeof(reg));
    }
  }
}

SYMS_INTERNAL void 
syms_regs_x64_xsave_put_regs(SymsIntelXSave *dest, struct SymsRegs *src)
{
  U8 reg[32];
  U32 n;

  syms_regs_x64_fxsave_put_regs(&dest->fxsave, src);

  dest->xsave_hdr.xstate_bv |= 7; // Indicate we changed FPU/SSE/AVX

  // Write YMM upper halves
  for (n = 0; n < 16; n++) {
    syms_regs_get_value(src, (SymsRegID)(SYMS_REG_X64_ymm0 + n), reg, sizeof(reg));
    syms_memcpy(&dest->ymmh[n*16], &reg[16], 16);
  }
}

// X86 --------------------------------------------------------------------

SYMS_COMPILER_ASSERT(SYMS_REG_X86_ymm7 == SYMS_REG_X86_ymm0 + 7);

SYMS_INTERNAL void 
syms_regs_x86_fxsave_get_regs(SymsRegs *dest, SymsIntelFXSave *src) 
{
  U32 n;
  U16 ftw = syms_regs_fix_tag_word(src);
  syms_regs_set_value(dest, SYMS_REG_X86_fcw, &src->fcw, sizeof(src->fcw));
  syms_regs_set_value(dest, SYMS_REG_X86_fsw, &src->fsw, sizeof(src->fsw));
  syms_regs_set_value(dest, SYMS_REG_X86_ftw, &ftw, sizeof(ftw));
  syms_regs_set_value(dest, SYMS_REG_X86_fop, &src->fop, sizeof(src->fop));
  syms_regs_set_value(dest, SYMS_REG_X86_fip, &src->u.format32.fip, sizeof(src->u.format32.fip));
  syms_regs_set_value(dest, SYMS_REG_X86_fcs, &src->u.format32.fcs, sizeof(src->u.format32.fcs));
  syms_regs_set_value(dest, SYMS_REG_X86_fdp, &src->u.format32.fdp, sizeof(src->u.format32.fdp));
  syms_regs_set_value(dest, SYMS_REG_X86_fds, &src->u.format32.fds, sizeof(src->u.format32.fds));
  syms_regs_set_value(dest, SYMS_REG_X86_mxcsr, &src->mxcsr, sizeof(src->mxcsr));
  for (n = 0; n < 8; n++) {
    syms_regs_set_value(dest, (SymsRegID)(SYMS_REG_X86_st0+n), &src->st[n*16], sizeof(src->st[0]));
  }
  for (n = 0; n < 8; n++) {
    syms_regs_set_value(dest, (SymsRegID)(SYMS_REG_X86_xmm0+n), &src->xmm[n*16], sizeof(src->xmm[0]));
  }
}

SYMS_INTERNAL void 
syms_regs_x86_fxsave_put_regs(SymsIntelFXSave *dest, struct SymsRegs *src)
{
  U32 n;
  syms_regs_get_value(src, SYMS_REG_X86_fcw, &dest->fcw, sizeof(dest->fcw));
  syms_regs_get_value(src, SYMS_REG_X86_fsw, &dest->fsw, sizeof(dest->fsw));
  syms_regs_get_value(src, SYMS_REG_X86_ftw, &dest->ftw, sizeof(dest->ftw));
  syms_regs_get_value(src, SYMS_REG_X86_fop, &dest->fop, sizeof(dest->fop));
  syms_regs_get_value(src, SYMS_REG_X86_fip, &dest->u.format32.fip, sizeof(dest->u.format32.fip));
  syms_regs_get_value(src, SYMS_REG_X86_fcs, &dest->u.format32.fcs, sizeof(dest->u.format32.fcs));
  syms_regs_get_value(src, SYMS_REG_X86_fdp, &dest->u.format32.fdp, sizeof(dest->u.format32.fdp));
  syms_regs_get_value(src, SYMS_REG_X86_fds, &dest->u.format32.fds, sizeof(dest->u.format32.fds));
  syms_regs_get_value(src, SYMS_REG_X86_mxcsr, &dest->mxcsr, sizeof(dest->mxcsr));
  for (n = 0; n < 8; n++) {
    syms_regs_get_value(src, (SymsRegID)(SYMS_REG_X86_st0+n), &dest->st[n*16], sizeof(dest->st[0]));
  }
  for (n = 0; n < 8; n++) {
    syms_regs_get_value(src, (SymsRegID)(SYMS_REG_X86_xmm0+n), &dest->xmm[n*16], sizeof(dest->xmm[0]));
  }

  dest->ftw = syms_regs_break_tag_word(dest->ftw);
}

SYMS_INTERNAL void 
syms_regs_x86_xsave_get_regs(SymsRegs *dest, SymsIntelXSave *src)
{
  syms_regs_x86_fxsave_get_regs(dest, &src->fxsave);

  // Check if YMM regs exist
  if (src->xsave_hdr.xstate_bv & 4) {
    U8 reg[32];
    U32 n;
    for (n = 0; n < 8; n++) {
      syms_memcpy(&reg[0], &src->fxsave.xmm[n*16], 16); // construct the two halves
      syms_memcpy(&reg[16], &src->ymmh[n*16], 16);
      syms_regs_set_value(dest, (SymsRegID)(SYMS_REG_X86_ymm0+n), reg, sizeof(reg));
    }
  }
}

SYMS_INTERNAL void 
syms_regs_x86_xsave_put_regs(SymsIntelXSave *dest, struct SymsRegs *src)
{
  U8 reg[32];
  U32 n;

  syms_regs_x86_fxsave_put_regs(&dest->fxsave, src);

  dest->xsave_hdr.xstate_bv |= 7; // Indicate we changed FPU/SSE/AVX

  // Write YMM upper halves
  for (n = 0; n < 8; n++) {
    syms_regs_get_value(src, (SymsRegID)(SYMS_REG_X86_ymm0+n), reg, sizeof(reg));
    syms_memcpy(&dest->ymmh[n*16], &reg[16], 16);
  }
}
