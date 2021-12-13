// Copyright Epic Games, Inc. All Rights Reserved.
// generated
#ifndef _SYMS_META_CV_C
#define _SYMS_META_CV_C
//~ generated from code at syms/metaprogram/syms_metaprogram_serial.c:1135
SYMS_API SYMS_RegID
syms_reg_from_pdb_reg_x86(SYMS_CvRegx86 v){
SYMS_RegID result = 0;
switch (v){
default: break;
case SYMS_CvRegx86_AL: result = SYMS_RegX86Code_al; break;
case SYMS_CvRegx86_CL: result = SYMS_RegX86Code_cl; break;
case SYMS_CvRegx86_DL: result = SYMS_RegX86Code_dl; break;
case SYMS_CvRegx86_BL: result = SYMS_RegX86Code_bl; break;
case SYMS_CvRegx86_AH: result = SYMS_RegX86Code_ah; break;
case SYMS_CvRegx86_CH: result = SYMS_RegX86Code_ch; break;
case SYMS_CvRegx86_DH: result = SYMS_RegX86Code_dh; break;
case SYMS_CvRegx86_BH: result = SYMS_RegX86Code_bh; break;
case SYMS_CvRegx86_AX: result = SYMS_RegX86Code_ax; break;
case SYMS_CvRegx86_CX: result = SYMS_RegX86Code_cx; break;
case SYMS_CvRegx86_DX: result = SYMS_RegX86Code_dx; break;
case SYMS_CvRegx86_BX: result = SYMS_RegX86Code_bx; break;
case SYMS_CvRegx86_SP: result = SYMS_RegX86Code_sp; break;
case SYMS_CvRegx86_BP: result = SYMS_RegX86Code_bp; break;
case SYMS_CvRegx86_SI: result = SYMS_RegX86Code_si; break;
case SYMS_CvRegx86_DI: result = SYMS_RegX86Code_di; break;
case SYMS_CvRegx86_EAX: result = SYMS_RegX86Code_eax; break;
case SYMS_CvRegx86_ECX: result = SYMS_RegX86Code_ecx; break;
case SYMS_CvRegx86_EDX: result = SYMS_RegX86Code_edx; break;
case SYMS_CvRegx86_EBX: result = SYMS_RegX86Code_ebx; break;
case SYMS_CvRegx86_ESP: result = SYMS_RegX86Code_esp; break;
case SYMS_CvRegx86_EBP: result = SYMS_RegX86Code_ebp; break;
case SYMS_CvRegx86_ESI: result = SYMS_RegX86Code_esi; break;
case SYMS_CvRegx86_EDI: result = SYMS_RegX86Code_edi; break;
case SYMS_CvRegx86_ES: result = SYMS_RegX86Code_es; break;
case SYMS_CvRegx86_CS: result = SYMS_RegX86Code_cs; break;
case SYMS_CvRegx86_SS: result = SYMS_RegX86Code_ss; break;
case SYMS_CvRegx86_DS: result = SYMS_RegX86Code_ds; break;
case SYMS_CvRegx86_FS: result = SYMS_RegX86Code_fs; break;
case SYMS_CvRegx86_GS: result = SYMS_RegX86Code_gs; break;
case SYMS_CvRegx86_IP: result = SYMS_RegX86Code_ip; break;
case SYMS_CvRegx86_EIP: result = SYMS_RegX86Code_eip; break;
case SYMS_CvRegx86_EFLAGS: result = SYMS_RegX86Code_eflags; break;
case SYMS_CvRegx86_MM0: result = SYMS_RegX86Code_mm0; break;
case SYMS_CvRegx86_MM1: result = SYMS_RegX86Code_mm1; break;
case SYMS_CvRegx86_MM2: result = SYMS_RegX86Code_mm2; break;
case SYMS_CvRegx86_MM3: result = SYMS_RegX86Code_mm3; break;
case SYMS_CvRegx86_MM4: result = SYMS_RegX86Code_mm4; break;
case SYMS_CvRegx86_MM5: result = SYMS_RegX86Code_mm5; break;
case SYMS_CvRegx86_MM6: result = SYMS_RegX86Code_mm6; break;
case SYMS_CvRegx86_MM7: result = SYMS_RegX86Code_mm7; break;
case SYMS_CvRegx86_XMM0: result = SYMS_RegX86Code_xmm0; break;
case SYMS_CvRegx86_XMM1: result = SYMS_RegX86Code_xmm1; break;
case SYMS_CvRegx86_XMM2: result = SYMS_RegX86Code_xmm2; break;
case SYMS_CvRegx86_XMM3: result = SYMS_RegX86Code_xmm3; break;
case SYMS_CvRegx86_XMM4: result = SYMS_RegX86Code_xmm4; break;
case SYMS_CvRegx86_XMM5: result = SYMS_RegX86Code_xmm5; break;
case SYMS_CvRegx86_XMM6: result = SYMS_RegX86Code_xmm6; break;
case SYMS_CvRegx86_XMM7: result = SYMS_RegX86Code_xmm7; break;
case SYMS_CvRegx86_YMM0: result = SYMS_RegX86Code_ymm0; break;
case SYMS_CvRegx86_YMM1: result = SYMS_RegX86Code_ymm1; break;
case SYMS_CvRegx86_YMM2: result = SYMS_RegX86Code_ymm2; break;
case SYMS_CvRegx86_YMM3: result = SYMS_RegX86Code_ymm3; break;
case SYMS_CvRegx86_YMM4: result = SYMS_RegX86Code_ymm4; break;
case SYMS_CvRegx86_YMM5: result = SYMS_RegX86Code_ymm5; break;
case SYMS_CvRegx86_YMM6: result = SYMS_RegX86Code_ymm6; break;
case SYMS_CvRegx86_YMM7: result = SYMS_RegX86Code_ymm7; break;
}
return(result);
}
SYMS_API SYMS_RegID
syms_reg_from_pdb_reg_x64(SYMS_CvRegx64 v){
SYMS_RegID result = 0;
switch (v){
default: break;
case SYMS_CvRegx64_AL: result = SYMS_RegX64Code_al; break;
case SYMS_CvRegx64_CL: result = SYMS_RegX64Code_cl; break;
case SYMS_CvRegx64_DL: result = SYMS_RegX64Code_dl; break;
case SYMS_CvRegx64_BL: result = SYMS_RegX64Code_bl; break;
case SYMS_CvRegx64_AH: result = SYMS_RegX64Code_ah; break;
case SYMS_CvRegx64_CH: result = SYMS_RegX64Code_ch; break;
case SYMS_CvRegx64_DH: result = SYMS_RegX64Code_dh; break;
case SYMS_CvRegx64_BH: result = SYMS_RegX64Code_bh; break;
case SYMS_CvRegx64_AX: result = SYMS_RegX64Code_ax; break;
case SYMS_CvRegx64_CX: result = SYMS_RegX64Code_cx; break;
case SYMS_CvRegx64_DX: result = SYMS_RegX64Code_dx; break;
case SYMS_CvRegx64_BX: result = SYMS_RegX64Code_bx; break;
case SYMS_CvRegx64_SP: result = SYMS_RegX64Code_sp; break;
case SYMS_CvRegx64_BP: result = SYMS_RegX64Code_bp; break;
case SYMS_CvRegx64_SI: result = SYMS_RegX64Code_si; break;
case SYMS_CvRegx64_DI: result = SYMS_RegX64Code_di; break;
case SYMS_CvRegx64_EAX: result = SYMS_RegX64Code_eax; break;
case SYMS_CvRegx64_ECX: result = SYMS_RegX64Code_ecx; break;
case SYMS_CvRegx64_EDX: result = SYMS_RegX64Code_edx; break;
case SYMS_CvRegx64_EBX: result = SYMS_RegX64Code_ebx; break;
case SYMS_CvRegx64_ESP: result = SYMS_RegX64Code_esp; break;
case SYMS_CvRegx64_EBP: result = SYMS_RegX64Code_ebp; break;
case SYMS_CvRegx64_ESI: result = SYMS_RegX64Code_esi; break;
case SYMS_CvRegx64_EDI: result = SYMS_RegX64Code_edi; break;
case SYMS_CvRegx64_ES: result = SYMS_RegX64Code_es; break;
case SYMS_CvRegx64_CS: result = SYMS_RegX64Code_cs; break;
case SYMS_CvRegx64_SS: result = SYMS_RegX64Code_ss; break;
case SYMS_CvRegx64_DS: result = SYMS_RegX64Code_ds; break;
case SYMS_CvRegx64_FS: result = SYMS_RegX64Code_fs; break;
case SYMS_CvRegx64_GS: result = SYMS_RegX64Code_gs; break;
case SYMS_CvRegx64_RIP: result = SYMS_RegX64Code_rip; break;
case SYMS_CvRegx64_EFLAGS: result = SYMS_RegX64Code_eflags; break;
case SYMS_CvRegx64_DR0: result = SYMS_RegX64Code_dr0; break;
case SYMS_CvRegx64_DR1: result = SYMS_RegX64Code_dr1; break;
case SYMS_CvRegx64_DR2: result = SYMS_RegX64Code_dr2; break;
case SYMS_CvRegx64_DR3: result = SYMS_RegX64Code_dr3; break;
case SYMS_CvRegx64_DR4: result = SYMS_RegX64Code_dr4; break;
case SYMS_CvRegx64_DR5: result = SYMS_RegX64Code_dr5; break;
case SYMS_CvRegx64_DR6: result = SYMS_RegX64Code_dr6; break;
case SYMS_CvRegx64_DR7: result = SYMS_RegX64Code_dr7; break;
case SYMS_CvRegx64_ST0: result = SYMS_RegX64Code_st0; break;
case SYMS_CvRegx64_ST1: result = SYMS_RegX64Code_st1; break;
case SYMS_CvRegx64_ST2: result = SYMS_RegX64Code_st2; break;
case SYMS_CvRegx64_ST3: result = SYMS_RegX64Code_st3; break;
case SYMS_CvRegx64_ST4: result = SYMS_RegX64Code_st4; break;
case SYMS_CvRegx64_ST5: result = SYMS_RegX64Code_st5; break;
case SYMS_CvRegx64_ST6: result = SYMS_RegX64Code_st6; break;
case SYMS_CvRegx64_ST7: result = SYMS_RegX64Code_st7; break;
case SYMS_CvRegx64_MM0: result = SYMS_RegX64Code_mm0; break;
case SYMS_CvRegx64_MM1: result = SYMS_RegX64Code_mm1; break;
case SYMS_CvRegx64_MM2: result = SYMS_RegX64Code_mm2; break;
case SYMS_CvRegx64_MM3: result = SYMS_RegX64Code_mm3; break;
case SYMS_CvRegx64_MM4: result = SYMS_RegX64Code_mm4; break;
case SYMS_CvRegx64_MM5: result = SYMS_RegX64Code_mm5; break;
case SYMS_CvRegx64_MM6: result = SYMS_RegX64Code_mm6; break;
case SYMS_CvRegx64_MM7: result = SYMS_RegX64Code_mm7; break;
case SYMS_CvRegx64_XMM0: result = SYMS_RegX64Code_xmm0; break;
case SYMS_CvRegx64_XMM1: result = SYMS_RegX64Code_xmm1; break;
case SYMS_CvRegx64_XMM2: result = SYMS_RegX64Code_xmm2; break;
case SYMS_CvRegx64_XMM3: result = SYMS_RegX64Code_xmm3; break;
case SYMS_CvRegx64_XMM4: result = SYMS_RegX64Code_xmm4; break;
case SYMS_CvRegx64_XMM5: result = SYMS_RegX64Code_xmm5; break;
case SYMS_CvRegx64_XMM6: result = SYMS_RegX64Code_xmm6; break;
case SYMS_CvRegx64_XMM7: result = SYMS_RegX64Code_xmm7; break;
case SYMS_CvRegx64_MXCSR: result = SYMS_RegX64Code_mxcsr; break;
case SYMS_CvRegx64_XMM8: result = SYMS_RegX64Code_xmm8; break;
case SYMS_CvRegx64_XMM9: result = SYMS_RegX64Code_xmm9; break;
case SYMS_CvRegx64_XMM10: result = SYMS_RegX64Code_xmm10; break;
case SYMS_CvRegx64_XMM11: result = SYMS_RegX64Code_xmm11; break;
case SYMS_CvRegx64_XMM12: result = SYMS_RegX64Code_xmm12; break;
case SYMS_CvRegx64_XMM13: result = SYMS_RegX64Code_xmm13; break;
case SYMS_CvRegx64_XMM14: result = SYMS_RegX64Code_xmm14; break;
case SYMS_CvRegx64_XMM15: result = SYMS_RegX64Code_xmm15; break;
case SYMS_CvRegx64_SIL: result = SYMS_RegX64Code_sil; break;
case SYMS_CvRegx64_DIL: result = SYMS_RegX64Code_dil; break;
case SYMS_CvRegx64_BPL: result = SYMS_RegX64Code_bpl; break;
case SYMS_CvRegx64_SPL: result = SYMS_RegX64Code_spl; break;
case SYMS_CvRegx64_RAX: result = SYMS_RegX64Code_rax; break;
case SYMS_CvRegx64_RBX: result = SYMS_RegX64Code_rbx; break;
case SYMS_CvRegx64_RCX: result = SYMS_RegX64Code_rcx; break;
case SYMS_CvRegx64_RDX: result = SYMS_RegX64Code_rdx; break;
case SYMS_CvRegx64_RSI: result = SYMS_RegX64Code_rsi; break;
case SYMS_CvRegx64_RDI: result = SYMS_RegX64Code_rdi; break;
case SYMS_CvRegx64_RBP: result = SYMS_RegX64Code_rbp; break;
case SYMS_CvRegx64_RSP: result = SYMS_RegX64Code_rsp; break;
case SYMS_CvRegx64_R8: result = SYMS_RegX64Code_r8; break;
case SYMS_CvRegx64_R9: result = SYMS_RegX64Code_r9; break;
case SYMS_CvRegx64_R10: result = SYMS_RegX64Code_r10; break;
case SYMS_CvRegx64_R11: result = SYMS_RegX64Code_r11; break;
case SYMS_CvRegx64_R12: result = SYMS_RegX64Code_r12; break;
case SYMS_CvRegx64_R13: result = SYMS_RegX64Code_r13; break;
case SYMS_CvRegx64_R14: result = SYMS_RegX64Code_r14; break;
case SYMS_CvRegx64_R15: result = SYMS_RegX64Code_r15; break;
case SYMS_CvRegx64_R8B: result = SYMS_RegX64Code_r8b; break;
case SYMS_CvRegx64_R9B: result = SYMS_RegX64Code_r9b; break;
case SYMS_CvRegx64_R10B: result = SYMS_RegX64Code_r10b; break;
case SYMS_CvRegx64_R11B: result = SYMS_RegX64Code_r11b; break;
case SYMS_CvRegx64_R12B: result = SYMS_RegX64Code_r12b; break;
case SYMS_CvRegx64_R13B: result = SYMS_RegX64Code_r13b; break;
case SYMS_CvRegx64_R14B: result = SYMS_RegX64Code_r14b; break;
case SYMS_CvRegx64_R15B: result = SYMS_RegX64Code_r15b; break;
case SYMS_CvRegx64_R8W: result = SYMS_RegX64Code_r8w; break;
case SYMS_CvRegx64_R9W: result = SYMS_RegX64Code_r9w; break;
case SYMS_CvRegx64_R10W: result = SYMS_RegX64Code_r10w; break;
case SYMS_CvRegx64_R11W: result = SYMS_RegX64Code_r11w; break;
case SYMS_CvRegx64_R12W: result = SYMS_RegX64Code_r12w; break;
case SYMS_CvRegx64_R13W: result = SYMS_RegX64Code_r13w; break;
case SYMS_CvRegx64_R14W: result = SYMS_RegX64Code_r14w; break;
case SYMS_CvRegx64_R15W: result = SYMS_RegX64Code_r15w; break;
case SYMS_CvRegx64_R8D: result = SYMS_RegX64Code_r8d; break;
case SYMS_CvRegx64_R9D: result = SYMS_RegX64Code_r9d; break;
case SYMS_CvRegx64_R10D: result = SYMS_RegX64Code_r10d; break;
case SYMS_CvRegx64_R11D: result = SYMS_RegX64Code_r11d; break;
case SYMS_CvRegx64_R12D: result = SYMS_RegX64Code_r12d; break;
case SYMS_CvRegx64_R13D: result = SYMS_RegX64Code_r13d; break;
case SYMS_CvRegx64_R14D: result = SYMS_RegX64Code_r14d; break;
case SYMS_CvRegx64_R15D: result = SYMS_RegX64Code_r15d; break;
case SYMS_CvRegx64_YMM0: result = SYMS_RegX64Code_ymm0; break;
case SYMS_CvRegx64_YMM1: result = SYMS_RegX64Code_ymm1; break;
case SYMS_CvRegx64_YMM2: result = SYMS_RegX64Code_ymm2; break;
case SYMS_CvRegx64_YMM3: result = SYMS_RegX64Code_ymm3; break;
case SYMS_CvRegx64_YMM4: result = SYMS_RegX64Code_ymm4; break;
case SYMS_CvRegx64_YMM5: result = SYMS_RegX64Code_ymm5; break;
case SYMS_CvRegx64_YMM6: result = SYMS_RegX64Code_ymm6; break;
case SYMS_CvRegx64_YMM7: result = SYMS_RegX64Code_ymm7; break;
case SYMS_CvRegx64_YMM8: result = SYMS_RegX64Code_ymm8; break;
case SYMS_CvRegx64_YMM9: result = SYMS_RegX64Code_ymm9; break;
case SYMS_CvRegx64_YMM10: result = SYMS_RegX64Code_ymm10; break;
case SYMS_CvRegx64_YMM11: result = SYMS_RegX64Code_ymm11; break;
case SYMS_CvRegx64_YMM12: result = SYMS_RegX64Code_ymm12; break;
case SYMS_CvRegx64_YMM13: result = SYMS_RegX64Code_ymm13; break;
case SYMS_CvRegx64_YMM14: result = SYMS_RegX64Code_ymm14; break;
case SYMS_CvRegx64_YMM15: result = SYMS_RegX64Code_ymm15; break;
}
return(result);
}
SYMS_API SYMS_Language
syms_cv_base_language_from_cv_language(SYMS_CvLanguage v){
SYMS_Language result = SYMS_Language_Null;
switch (v){
default: break;
case SYMS_CvLanguage_C: result = SYMS_Language_C; break;
case SYMS_CvLanguage_CXX: result = SYMS_Language_CPlusPlus; break;
case SYMS_CvLanguage_FORTRAN: result = SYMS_Language_Fortran; break;
case SYMS_CvLanguage_MASM: result = SYMS_Language_MASM; break;
case SYMS_CvLanguage_PASCAL: result = SYMS_Language_Pascal; break;
case SYMS_CvLanguage_BASIC: result = SYMS_Language_Basic; break;
case SYMS_CvLanguage_COBOL: result = SYMS_Language_Cobol; break;
case SYMS_CvLanguage_LINK: result = SYMS_Language_Link; break;
case SYMS_CvLanguage_CVTRES: result = SYMS_Language_CVTRES; break;
case SYMS_CvLanguage_CVTPGD: result = SYMS_Language_CVTPGD; break;
case SYMS_CvLanguage_CSHARP: result = SYMS_Language_CSharp; break;
case SYMS_CvLanguage_VB: result = SYMS_Language_VisualBasic; break;
case SYMS_CvLanguage_ILASM: result = SYMS_Language_ILASM; break;
case SYMS_CvLanguage_JAVA: result = SYMS_Language_Java; break;
case SYMS_CvLanguage_JSCRIPT: result = SYMS_Language_JavaScript; break;
case SYMS_CvLanguage_MSIL: result = SYMS_Language_MSIL; break;
case SYMS_CvLanguage_HLSL: result = SYMS_Language_HLSL; break;
}
return(result);
}
SYMS_API SYMS_MemVisibility
syms_mem_visibility_from_member_access(SYMS_CvMemberAccess v){
SYMS_MemVisibility result = SYMS_MemVisibility_Null;
switch (v){
default: break;
case SYMS_CvMemberAccess_NULL: result = SYMS_MemVisibility_Null; break;
case SYMS_CvMemberAccess_PRIVATE: result = SYMS_MemVisibility_Private; break;
case SYMS_CvMemberAccess_PROTECTED: result = SYMS_MemVisibility_Protected; break;
case SYMS_CvMemberAccess_PUBLIC: result = SYMS_MemVisibility_Public; break;
}
return(result);
}

//~ generated from code at syms/metaprogram/syms_metaprogram_serial.c:1591
#endif
