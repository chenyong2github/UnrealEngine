// Copyright Epic Games, Inc. All Rights Reserved.
// generated
#ifndef _SYMS_META_REGS_X64_H
#define _SYMS_META_REGS_X64_H
//~ generated from code at syms/metaprogram/syms_metaprogram_regs.c:322
typedef struct SYMS_RegX64{
SYMS_Reg64 rax;
SYMS_Reg64 rcx;
SYMS_Reg64 rdx;
SYMS_Reg64 rbx;
SYMS_Reg64 rsp;
SYMS_Reg64 rbp;
SYMS_Reg64 rsi;
SYMS_Reg64 rdi;
SYMS_Reg64 r8;
SYMS_Reg64 r9;
SYMS_Reg64 r10;
SYMS_Reg64 r11;
SYMS_Reg64 r12;
SYMS_Reg64 r13;
SYMS_Reg64 r14;
SYMS_Reg64 r15;
SYMS_Reg64 fsbase;
SYMS_Reg64 gsbase;
SYMS_Reg64 rip;
SYMS_Reg64 rflags;
SYMS_Reg256 ymm0;
SYMS_Reg256 ymm1;
SYMS_Reg256 ymm2;
SYMS_Reg256 ymm3;
SYMS_Reg256 ymm4;
SYMS_Reg256 ymm5;
SYMS_Reg256 ymm6;
SYMS_Reg256 ymm7;
SYMS_Reg256 ymm8;
SYMS_Reg256 ymm9;
SYMS_Reg256 ymm10;
SYMS_Reg256 ymm11;
SYMS_Reg256 ymm12;
SYMS_Reg256 ymm13;
SYMS_Reg256 ymm14;
SYMS_Reg256 ymm15;
SYMS_Reg32 dr0;
SYMS_Reg32 dr1;
SYMS_Reg32 dr2;
SYMS_Reg32 dr3;
SYMS_Reg32 dr4;
SYMS_Reg32 dr5;
SYMS_Reg32 dr6;
SYMS_Reg32 dr7;
SYMS_Reg80 fpr0;
SYMS_Reg80 fpr1;
SYMS_Reg80 fpr2;
SYMS_Reg80 fpr3;
SYMS_Reg80 fpr4;
SYMS_Reg80 fpr5;
SYMS_Reg80 fpr6;
SYMS_Reg80 fpr7;
SYMS_Reg80 st0;
SYMS_Reg80 st1;
SYMS_Reg80 st2;
SYMS_Reg80 st3;
SYMS_Reg80 st4;
SYMS_Reg80 st5;
SYMS_Reg80 st6;
SYMS_Reg80 st7;
SYMS_Reg16 fcw;
SYMS_Reg16 fsw;
SYMS_Reg16 ftw;
SYMS_Reg16 fop;
SYMS_Reg16 fcs;
SYMS_Reg16 fds;
SYMS_Reg32 fip;
SYMS_Reg32 fdp;
SYMS_Reg32 mxcsr;
SYMS_Reg32 mxcsr_mask;
SYMS_Reg16 ss;
SYMS_Reg16 cs;
SYMS_Reg16 ds;
SYMS_Reg16 es;
SYMS_Reg16 fs;
SYMS_Reg16 gs;
} SYMS_RegX64;
//~ generated from code at syms/metaprogram/syms_metaprogram_regs.c:341
typedef enum SYMS_RegX64Code
{
SYMS_RegX64Code_Null,
SYMS_RegX64Code_rax,
SYMS_RegX64Code_rcx,
SYMS_RegX64Code_rdx,
SYMS_RegX64Code_rbx,
SYMS_RegX64Code_rsp,
SYMS_RegX64Code_rbp,
SYMS_RegX64Code_rsi,
SYMS_RegX64Code_rdi,
SYMS_RegX64Code_r8,
SYMS_RegX64Code_r9,
SYMS_RegX64Code_r10,
SYMS_RegX64Code_r11,
SYMS_RegX64Code_r12,
SYMS_RegX64Code_r13,
SYMS_RegX64Code_r14,
SYMS_RegX64Code_r15,
SYMS_RegX64Code_fsbase,
SYMS_RegX64Code_gsbase,
SYMS_RegX64Code_rip,
SYMS_RegX64Code_rflags,
SYMS_RegX64Code_ymm0,
SYMS_RegX64Code_ymm1,
SYMS_RegX64Code_ymm2,
SYMS_RegX64Code_ymm3,
SYMS_RegX64Code_ymm4,
SYMS_RegX64Code_ymm5,
SYMS_RegX64Code_ymm6,
SYMS_RegX64Code_ymm7,
SYMS_RegX64Code_ymm8,
SYMS_RegX64Code_ymm9,
SYMS_RegX64Code_ymm10,
SYMS_RegX64Code_ymm11,
SYMS_RegX64Code_ymm12,
SYMS_RegX64Code_ymm13,
SYMS_RegX64Code_ymm14,
SYMS_RegX64Code_ymm15,
SYMS_RegX64Code_dr0,
SYMS_RegX64Code_dr1,
SYMS_RegX64Code_dr2,
SYMS_RegX64Code_dr3,
SYMS_RegX64Code_dr4,
SYMS_RegX64Code_dr5,
SYMS_RegX64Code_dr6,
SYMS_RegX64Code_dr7,
SYMS_RegX64Code_fpr0,
SYMS_RegX64Code_fpr1,
SYMS_RegX64Code_fpr2,
SYMS_RegX64Code_fpr3,
SYMS_RegX64Code_fpr4,
SYMS_RegX64Code_fpr5,
SYMS_RegX64Code_fpr6,
SYMS_RegX64Code_fpr7,
SYMS_RegX64Code_st0,
SYMS_RegX64Code_st1,
SYMS_RegX64Code_st2,
SYMS_RegX64Code_st3,
SYMS_RegX64Code_st4,
SYMS_RegX64Code_st5,
SYMS_RegX64Code_st6,
SYMS_RegX64Code_st7,
SYMS_RegX64Code_fcw,
SYMS_RegX64Code_fsw,
SYMS_RegX64Code_ftw,
SYMS_RegX64Code_fop,
SYMS_RegX64Code_fcs,
SYMS_RegX64Code_fds,
SYMS_RegX64Code_fip,
SYMS_RegX64Code_fdp,
SYMS_RegX64Code_mxcsr,
SYMS_RegX64Code_mxcsr_mask,
SYMS_RegX64Code_ss,
SYMS_RegX64Code_cs,
SYMS_RegX64Code_ds,
SYMS_RegX64Code_es,
SYMS_RegX64Code_fs,
SYMS_RegX64Code_gs,
// ALIASES BEGIN
SYMS_RegX64Code_eax,
SYMS_RegX64Code_ecx,
SYMS_RegX64Code_edx,
SYMS_RegX64Code_ebx,
SYMS_RegX64Code_esp,
SYMS_RegX64Code_ebp,
SYMS_RegX64Code_esi,
SYMS_RegX64Code_edi,
SYMS_RegX64Code_r8d,
SYMS_RegX64Code_r9d,
SYMS_RegX64Code_r10d,
SYMS_RegX64Code_r11d,
SYMS_RegX64Code_r12d,
SYMS_RegX64Code_r13d,
SYMS_RegX64Code_r14d,
SYMS_RegX64Code_r15d,
SYMS_RegX64Code_eip,
SYMS_RegX64Code_eflags,
SYMS_RegX64Code_ax,
SYMS_RegX64Code_cx,
SYMS_RegX64Code_dx,
SYMS_RegX64Code_bx,
SYMS_RegX64Code_si,
SYMS_RegX64Code_di,
SYMS_RegX64Code_sp,
SYMS_RegX64Code_bp,
SYMS_RegX64Code_ip,
SYMS_RegX64Code_r8w,
SYMS_RegX64Code_r9w,
SYMS_RegX64Code_r10w,
SYMS_RegX64Code_r11w,
SYMS_RegX64Code_r12w,
SYMS_RegX64Code_r13w,
SYMS_RegX64Code_r14w,
SYMS_RegX64Code_r15w,
SYMS_RegX64Code_al,
SYMS_RegX64Code_cl,
SYMS_RegX64Code_dl,
SYMS_RegX64Code_bl,
SYMS_RegX64Code_sil,
SYMS_RegX64Code_dil,
SYMS_RegX64Code_bpl,
SYMS_RegX64Code_spl,
SYMS_RegX64Code_r8b,
SYMS_RegX64Code_r9b,
SYMS_RegX64Code_r10b,
SYMS_RegX64Code_r11b,
SYMS_RegX64Code_r12b,
SYMS_RegX64Code_r13b,
SYMS_RegX64Code_r14b,
SYMS_RegX64Code_r15b,
SYMS_RegX64Code_ah,
SYMS_RegX64Code_ch,
SYMS_RegX64Code_dh,
SYMS_RegX64Code_bh,
SYMS_RegX64Code_xmm0,
SYMS_RegX64Code_xmm1,
SYMS_RegX64Code_xmm2,
SYMS_RegX64Code_xmm3,
SYMS_RegX64Code_xmm4,
SYMS_RegX64Code_xmm5,
SYMS_RegX64Code_xmm6,
SYMS_RegX64Code_xmm7,
SYMS_RegX64Code_xmm8,
SYMS_RegX64Code_xmm9,
SYMS_RegX64Code_xmm10,
SYMS_RegX64Code_xmm11,
SYMS_RegX64Code_xmm12,
SYMS_RegX64Code_xmm13,
SYMS_RegX64Code_xmm14,
SYMS_RegX64Code_xmm15,
SYMS_RegX64Code_mm0,
SYMS_RegX64Code_mm1,
SYMS_RegX64Code_mm2,
SYMS_RegX64Code_mm3,
SYMS_RegX64Code_mm4,
SYMS_RegX64Code_mm5,
SYMS_RegX64Code_mm6,
SYMS_RegX64Code_mm7,
SYMS_RegX64Code_COUNT
} SYMS_RegX64Code;
//~ generated from code at syms/metaprogram/syms_metaprogram_regs.c:367
static SYMS_RegMetadata reg_metadata_X64[SYMS_RegX64Code_COUNT] = {
{0, 0, {(SYMS_U8*)"<nil>", 5}, 0, 0},
{0, 8, {(SYMS_U8*)"rax", 3}, REG_ClassX86X64_GPR, 0},
{0, 8, {(SYMS_U8*)"rcx", 3}, REG_ClassX86X64_GPR, 0},
{0, 8, {(SYMS_U8*)"rdx", 3}, REG_ClassX86X64_GPR, 0},
{0, 8, {(SYMS_U8*)"rbx", 3}, REG_ClassX86X64_GPR, 0},
{0, 8, {(SYMS_U8*)"rsp", 3}, REG_ClassX86X64_GPR, 0},
{0, 8, {(SYMS_U8*)"rbp", 3}, REG_ClassX86X64_GPR, 0},
{0, 8, {(SYMS_U8*)"rsi", 3}, REG_ClassX86X64_GPR, 0},
{0, 8, {(SYMS_U8*)"rdi", 3}, REG_ClassX86X64_GPR, 0},
{0, 8, {(SYMS_U8*)"r8", 2}, REG_ClassX86X64_GPR, 0},
{0, 8, {(SYMS_U8*)"r9", 2}, REG_ClassX86X64_GPR, 0},
{0, 8, {(SYMS_U8*)"r10", 3}, REG_ClassX86X64_GPR, 0},
{0, 8, {(SYMS_U8*)"r11", 3}, REG_ClassX86X64_GPR, 0},
{0, 8, {(SYMS_U8*)"r12", 3}, REG_ClassX86X64_GPR, 0},
{0, 8, {(SYMS_U8*)"r13", 3}, REG_ClassX86X64_GPR, 0},
{0, 8, {(SYMS_U8*)"r14", 3}, REG_ClassX86X64_GPR, 0},
{0, 8, {(SYMS_U8*)"r15", 3}, REG_ClassX86X64_GPR, 0},
{0, 8, {(SYMS_U8*)"fsbase", 6}, REG_ClassX86X64_GPR, 0},
{0, 8, {(SYMS_U8*)"gsbase", 6}, REG_ClassX86X64_GPR, 0},
{0, 8, {(SYMS_U8*)"rip", 3}, REG_ClassX86X64_STATE, 0},
{0, 8, {(SYMS_U8*)"rflags", 6}, REG_ClassX86X64_STATE, 0},
{0, 32, {(SYMS_U8*)"ymm0", 4}, REG_ClassX86X64_VEC, 0},
{0, 32, {(SYMS_U8*)"ymm1", 4}, REG_ClassX86X64_VEC, 0},
{0, 32, {(SYMS_U8*)"ymm2", 4}, REG_ClassX86X64_VEC, 0},
{0, 32, {(SYMS_U8*)"ymm3", 4}, REG_ClassX86X64_VEC, 0},
{0, 32, {(SYMS_U8*)"ymm4", 4}, REG_ClassX86X64_VEC, 0},
{0, 32, {(SYMS_U8*)"ymm5", 4}, REG_ClassX86X64_VEC, 0},
{0, 32, {(SYMS_U8*)"ymm6", 4}, REG_ClassX86X64_VEC, 0},
{0, 32, {(SYMS_U8*)"ymm7", 4}, REG_ClassX86X64_VEC, 0},
{0, 32, {(SYMS_U8*)"ymm8", 4}, REG_ClassX86X64_VEC, 0},
{0, 32, {(SYMS_U8*)"ymm9", 4}, REG_ClassX86X64_VEC, 0},
{0, 32, {(SYMS_U8*)"ymm10", 5}, REG_ClassX86X64_VEC, 0},
{0, 32, {(SYMS_U8*)"ymm11", 5}, REG_ClassX86X64_VEC, 0},
{0, 32, {(SYMS_U8*)"ymm12", 5}, REG_ClassX86X64_VEC, 0},
{0, 32, {(SYMS_U8*)"ymm13", 5}, REG_ClassX86X64_VEC, 0},
{0, 32, {(SYMS_U8*)"ymm14", 5}, REG_ClassX86X64_VEC, 0},
{0, 32, {(SYMS_U8*)"ymm15", 5}, REG_ClassX86X64_VEC, 0},
{0, 4, {(SYMS_U8*)"dr0", 3}, REG_ClassX86X64_CTRL, 0},
{0, 4, {(SYMS_U8*)"dr1", 3}, REG_ClassX86X64_CTRL, 0},
{0, 4, {(SYMS_U8*)"dr2", 3}, REG_ClassX86X64_CTRL, 0},
{0, 4, {(SYMS_U8*)"dr3", 3}, REG_ClassX86X64_CTRL, 0},
{0, 4, {(SYMS_U8*)"dr4", 3}, REG_ClassX86X64_CTRL, 0},
{0, 4, {(SYMS_U8*)"dr5", 3}, REG_ClassX86X64_CTRL, 0},
{0, 4, {(SYMS_U8*)"dr6", 3}, REG_ClassX86X64_CTRL, 0},
{0, 4, {(SYMS_U8*)"dr7", 3}, REG_ClassX86X64_CTRL, 0},
{0, 10, {(SYMS_U8*)"fpr0", 4}, REG_ClassX86X64_FP, 0},
{0, 10, {(SYMS_U8*)"fpr1", 4}, REG_ClassX86X64_FP, 0},
{0, 10, {(SYMS_U8*)"fpr2", 4}, REG_ClassX86X64_FP, 0},
{0, 10, {(SYMS_U8*)"fpr3", 4}, REG_ClassX86X64_FP, 0},
{0, 10, {(SYMS_U8*)"fpr4", 4}, REG_ClassX86X64_FP, 0},
{0, 10, {(SYMS_U8*)"fpr5", 4}, REG_ClassX86X64_FP, 0},
{0, 10, {(SYMS_U8*)"fpr6", 4}, REG_ClassX86X64_FP, 0},
{0, 10, {(SYMS_U8*)"fpr7", 4}, REG_ClassX86X64_FP, 0},
{0, 10, {(SYMS_U8*)"st0", 3}, REG_ClassX86X64_FP, 0},
{0, 10, {(SYMS_U8*)"st1", 3}, REG_ClassX86X64_FP, 0},
{0, 10, {(SYMS_U8*)"st2", 3}, REG_ClassX86X64_FP, 0},
{0, 10, {(SYMS_U8*)"st3", 3}, REG_ClassX86X64_FP, 0},
{0, 10, {(SYMS_U8*)"st4", 3}, REG_ClassX86X64_FP, 0},
{0, 10, {(SYMS_U8*)"st5", 3}, REG_ClassX86X64_FP, 0},
{0, 10, {(SYMS_U8*)"st6", 3}, REG_ClassX86X64_FP, 0},
{0, 10, {(SYMS_U8*)"st7", 3}, REG_ClassX86X64_FP, 0},
{0, 2, {(SYMS_U8*)"fcw", 3}, REG_ClassX86X64_CTRL, 0},
{0, 2, {(SYMS_U8*)"fsw", 3}, REG_ClassX86X64_CTRL, 0},
{0, 2, {(SYMS_U8*)"ftw", 3}, REG_ClassX86X64_CTRL, 0},
{0, 2, {(SYMS_U8*)"fop", 3}, REG_ClassX86X64_CTRL, 0},
{0, 2, {(SYMS_U8*)"fcs", 3}, REG_ClassX86X64_CTRL, 0},
{0, 2, {(SYMS_U8*)"fds", 3}, REG_ClassX86X64_CTRL, 0},
{0, 4, {(SYMS_U8*)"fip", 3}, REG_ClassX86X64_CTRL, 0},
{0, 4, {(SYMS_U8*)"fdp", 3}, REG_ClassX86X64_CTRL, 0},
{0, 4, {(SYMS_U8*)"mxcsr", 5}, REG_ClassX86X64_CTRL, 0},
{0, 4, {(SYMS_U8*)"mxcsr_mask", 10}, REG_ClassX86X64_CTRL, 0},
{0, 2, {(SYMS_U8*)"ss", 2}, REG_ClassX86X64_GPR, 0},
{0, 2, {(SYMS_U8*)"cs", 2}, REG_ClassX86X64_GPR, 0},
{0, 2, {(SYMS_U8*)"ds", 2}, REG_ClassX86X64_GPR, 0},
{0, 2, {(SYMS_U8*)"es", 2}, REG_ClassX86X64_GPR, 0},
{0, 2, {(SYMS_U8*)"fs", 2}, REG_ClassX86X64_GPR, 0},
{0, 2, {(SYMS_U8*)"gs", 2}, REG_ClassX86X64_GPR, 0},
// ALIASES BEGIN
{0, 4, {(SYMS_U8*)"eax", 3}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rax},
{0, 4, {(SYMS_U8*)"ecx", 3}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rcx},
{0, 4, {(SYMS_U8*)"edx", 3}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rdx},
{0, 4, {(SYMS_U8*)"ebx", 3}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rbx},
{0, 4, {(SYMS_U8*)"esp", 3}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rsp},
{0, 4, {(SYMS_U8*)"ebp", 3}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rbp},
{0, 4, {(SYMS_U8*)"esi", 3}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rsi},
{0, 4, {(SYMS_U8*)"edi", 3}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rdi},
{0, 4, {(SYMS_U8*)"r8d", 3}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r8},
{0, 4, {(SYMS_U8*)"r9d", 3}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r9},
{0, 4, {(SYMS_U8*)"r10d", 4}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r10},
{0, 4, {(SYMS_U8*)"r11d", 4}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r11},
{0, 4, {(SYMS_U8*)"r12d", 4}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r12},
{0, 4, {(SYMS_U8*)"r13d", 4}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r13},
{0, 4, {(SYMS_U8*)"r14d", 4}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r14},
{0, 4, {(SYMS_U8*)"r15d", 4}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r15},
{0, 4, {(SYMS_U8*)"eip", 3}, REG_ClassX86X64_STATE, SYMS_RegX64Code_rip},
{0, 4, {(SYMS_U8*)"eflags", 6}, REG_ClassX86X64_STATE, SYMS_RegX64Code_rflags},
{0, 2, {(SYMS_U8*)"ax", 2}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rax},
{0, 2, {(SYMS_U8*)"cx", 2}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rcx},
{0, 2, {(SYMS_U8*)"dx", 2}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rdx},
{0, 2, {(SYMS_U8*)"bx", 2}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rbx},
{0, 2, {(SYMS_U8*)"si", 2}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rsi},
{0, 2, {(SYMS_U8*)"di", 2}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rdi},
{0, 2, {(SYMS_U8*)"sp", 2}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rsp},
{0, 2, {(SYMS_U8*)"bp", 2}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rbp},
{0, 2, {(SYMS_U8*)"ip", 2}, REG_ClassX86X64_STATE, SYMS_RegX64Code_rip},
{0, 2, {(SYMS_U8*)"r8w", 3}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r8},
{0, 2, {(SYMS_U8*)"r9w", 3}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r9},
{0, 2, {(SYMS_U8*)"r10w", 4}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r10},
{0, 2, {(SYMS_U8*)"r11w", 4}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r11},
{0, 2, {(SYMS_U8*)"r12w", 4}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r12},
{0, 2, {(SYMS_U8*)"r13w", 4}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r13},
{0, 2, {(SYMS_U8*)"r14w", 4}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r14},
{0, 2, {(SYMS_U8*)"r15w", 4}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r15},
{0, 1, {(SYMS_U8*)"al", 2}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rax},
{0, 1, {(SYMS_U8*)"cl", 2}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rcx},
{0, 1, {(SYMS_U8*)"dl", 2}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rdx},
{0, 1, {(SYMS_U8*)"bl", 2}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rbx},
{0, 1, {(SYMS_U8*)"sil", 3}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rsi},
{0, 1, {(SYMS_U8*)"dil", 3}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rdi},
{0, 1, {(SYMS_U8*)"bpl", 3}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rbp},
{0, 1, {(SYMS_U8*)"spl", 3}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rsp},
{0, 1, {(SYMS_U8*)"r8b", 3}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r8},
{0, 1, {(SYMS_U8*)"r9b", 3}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r9},
{0, 1, {(SYMS_U8*)"r10b", 4}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r10},
{0, 1, {(SYMS_U8*)"r11b", 4}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r11},
{0, 1, {(SYMS_U8*)"r12b", 4}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r12},
{0, 1, {(SYMS_U8*)"r13b", 4}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r13},
{0, 1, {(SYMS_U8*)"r14b", 4}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r14},
{0, 1, {(SYMS_U8*)"r15b", 4}, REG_ClassX86X64_GPR, SYMS_RegX64Code_r15},
{1, 1, {(SYMS_U8*)"ah", 2}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rax},
{1, 1, {(SYMS_U8*)"ch", 2}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rcx},
{1, 1, {(SYMS_U8*)"dh", 2}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rdx},
{1, 1, {(SYMS_U8*)"bh", 2}, REG_ClassX86X64_GPR, SYMS_RegX64Code_rbx},
{0, 16, {(SYMS_U8*)"xmm0", 4}, REG_ClassX86X64_VEC, SYMS_RegX64Code_ymm0},
{0, 16, {(SYMS_U8*)"xmm1", 4}, REG_ClassX86X64_VEC, SYMS_RegX64Code_ymm1},
{0, 16, {(SYMS_U8*)"xmm2", 4}, REG_ClassX86X64_VEC, SYMS_RegX64Code_ymm2},
{0, 16, {(SYMS_U8*)"xmm3", 4}, REG_ClassX86X64_VEC, SYMS_RegX64Code_ymm3},
{0, 16, {(SYMS_U8*)"xmm4", 4}, REG_ClassX86X64_VEC, SYMS_RegX64Code_ymm4},
{0, 16, {(SYMS_U8*)"xmm5", 4}, REG_ClassX86X64_VEC, SYMS_RegX64Code_ymm5},
{0, 16, {(SYMS_U8*)"xmm6", 4}, REG_ClassX86X64_VEC, SYMS_RegX64Code_ymm6},
{0, 16, {(SYMS_U8*)"xmm7", 4}, REG_ClassX86X64_VEC, SYMS_RegX64Code_ymm7},
{0, 16, {(SYMS_U8*)"xmm8", 4}, REG_ClassX86X64_VEC, SYMS_RegX64Code_ymm8},
{0, 16, {(SYMS_U8*)"xmm9", 4}, REG_ClassX86X64_VEC, SYMS_RegX64Code_ymm9},
{0, 16, {(SYMS_U8*)"xmm10", 5}, REG_ClassX86X64_VEC, SYMS_RegX64Code_ymm10},
{0, 16, {(SYMS_U8*)"xmm11", 5}, REG_ClassX86X64_VEC, SYMS_RegX64Code_ymm11},
{0, 16, {(SYMS_U8*)"xmm12", 5}, REG_ClassX86X64_VEC, SYMS_RegX64Code_ymm12},
{0, 16, {(SYMS_U8*)"xmm13", 5}, REG_ClassX86X64_VEC, SYMS_RegX64Code_ymm13},
{0, 16, {(SYMS_U8*)"xmm14", 5}, REG_ClassX86X64_VEC, SYMS_RegX64Code_ymm14},
{0, 16, {(SYMS_U8*)"xmm15", 5}, REG_ClassX86X64_VEC, SYMS_RegX64Code_ymm15},
{0, 8, {(SYMS_U8*)"mm0", 3}, REG_ClassX86X64_FP, SYMS_RegX64Code_fpr0},
{0, 8, {(SYMS_U8*)"mm1", 3}, REG_ClassX86X64_FP, SYMS_RegX64Code_fpr1},
{0, 8, {(SYMS_U8*)"mm2", 3}, REG_ClassX86X64_FP, SYMS_RegX64Code_fpr2},
{0, 8, {(SYMS_U8*)"mm3", 3}, REG_ClassX86X64_FP, SYMS_RegX64Code_fpr3},
{0, 8, {(SYMS_U8*)"mm4", 3}, REG_ClassX86X64_FP, SYMS_RegX64Code_fpr4},
{0, 8, {(SYMS_U8*)"mm5", 3}, REG_ClassX86X64_FP, SYMS_RegX64Code_fpr5},
{0, 8, {(SYMS_U8*)"mm6", 3}, REG_ClassX86X64_FP, SYMS_RegX64Code_fpr6},
{0, 8, {(SYMS_U8*)"mm7", 3}, REG_ClassX86X64_FP, SYMS_RegX64Code_fpr7},
};

#endif
