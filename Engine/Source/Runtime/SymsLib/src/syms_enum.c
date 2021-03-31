// Copyright Epic Games, Inc. All Rights Reserved.
SYMS_API const char *
syms_arch_to_str(SymsArch arch) {
  const char *str = "";
  switch (arch) {
  case SYMS_ARCH_NULL:    str = "NULL";   break;
  case SYMS_ARCH_ARM:     str = "ARM";    break;
  case SYMS_ARCH_ARM32:   str = "ARM32";  break;
  case SYMS_ARCH_PPC:     str = "PPC";    break;
  case SYMS_ARCH_PPC64:   str = "PPC64";  break;
  case SYMS_ARCH_IA64:    str = "IA64";   break;
  case SYMS_ARCH_X64:     str = "X64";    break;
  case SYMS_ARCH_X86:     str = "X86";    break;
  }
  return str;
}

SYMS_API const char *
syms_reg_group_to_str(SymsRegClass group)
{
  const char *str = "";
  switch (group) {
  case SYMS_REG_CLASS_NULL:    str = "NULL";    break;
  case SYMS_REG_CLASS_STATE:   str = "STATE";   break;
  case SYMS_REG_CLASS_GPR:     str = "GPR";     break;
  case SYMS_REG_CLASS_CTRL:    str = "CTRL";    break;
  case SYMS_REG_CLASS_FP:      str = "FP";      break;
  case SYMS_REG_CLASS_VEC:     str = "VEC";     break;
  case SYMS_REG_CLASS_INVALID: str = "INVALID"; break;
  }
  return str;
}

SYMS_API const char *
syms_member_access_to_str(SymsMemberAccess access)
{
  const char *str = "";
  switch (access) {
  case SYMS_MEMBER_ACCESS_NULL:      str = "NULL";      break;
  case SYMS_MEMBER_ACCESS_PRIVATE:   str = "PRIVATE";   break;
  case SYMS_MEMBER_ACCESS_PUBLIC:    str = "PUBLIC";    break;
  case SYMS_MEMBER_ACCESS_PROTECTED: str = "PROTECTED"; break;
  }
  return str;
}

SYMS_API const char *
syms_member_modifier_to_str(SymsMemberModifier modifier)
{
  const char *str = "";
  switch (modifier) {
  case SYMS_MEMBER_MODIFIER_NULL:         str = "NULL";         break;
  case SYMS_MEMBER_MODIFIER_VANILLA:      str = "VANILLA";      break;
  case SYMS_MEMBER_MODIFIER_STATIC:       str = "STATIC";       break;
  case SYMS_MEMBER_MODIFIER_FRIEND:       str = "FRIEND";       break;
  case SYMS_MEMBER_MODIFIER_VIRTUAL:      str = "VIRTUAL";      break;
  case SYMS_MEMBER_MODIFIER_PURE_INTRO:   str = "PURE_INTRO";   break;
  case SYMS_MEMBER_MODIFIER_INTRO:        str = "INTRO";        break;
  case SYMS_MEMBER_MODIFIER_PURE_VIRTUAL: str = "PURE_VIRTUAL"; break;
  }
  return str;
}

SYMS_API const char *
syms_type_modifier_to_str(SymsTypeModifier modifier)
{
  const char *str = "";
  switch (modifier) {
  case SYMS_TYPE_MDFR_NULL:       str = "NULL";       break;
  case SYMS_TYPE_MDFR_ATOMIC:     str = "ATOMIC";     break;
  case SYMS_TYPE_MDFR_CONST:      str = "CONST";      break;
  case SYMS_TYPE_MDFR_IMMUTABLE:  str = "IMMUTABLE";  break;
  case SYMS_TYPE_MDFR_REF:        str = "REF";        break;
  case SYMS_TYPE_MDFR_RESTRICT:   str = "RESTRICT";   break;
  case SYMS_TYPE_MDFR_RVALUE_REF: str = "RVALUE_REF"; break;
  case SYMS_TYPE_MDFR_SHARED:     str = "SHARED";     break;
  case SYMS_TYPE_MDFR_VOLATILE:   str = "VOLATILE";   break;
  }
  return str;
}

SYMS_API const char *
syms_get_type_kind_info(SymsTypeKind kind)
{
  const char *p = "";
  switch (kind) {
  case SYMS_TYPE_NULL:          p = "NULL";       break;
  case SYMS_TYPE_INT8:          p = "INT8";       break;
  case SYMS_TYPE_INT16:         p = "INT16";      break;
  case SYMS_TYPE_INT32:         p = "INT32";      break;
  case SYMS_TYPE_INT64:         p = "INT64";      break;
  case SYMS_TYPE_INT128:        p = "INT128";     break;
  case SYMS_TYPE_INT256:        p = "INT256";     break;
  case SYMS_TYPE_INT512:        p = "INT512";     break;
  case SYMS_TYPE_UINT8:         p = "UINT8";      break;
  case SYMS_TYPE_UINT16:        p = "UINT16";     break;
  case SYMS_TYPE_UINT32:        p = "UINT32";     break;
  case SYMS_TYPE_UINT64:        p = "UINT64";     break;
  case SYMS_TYPE_UINT128:       p = "UINT128";    break;
  case SYMS_TYPE_UINT256:       p = "UINT256";    break;
  case SYMS_TYPE_UINT512:       p = "UINT512";    break;
  case SYMS_TYPE_FLOAT16:       p = "FLOAT16";    break;
  case SYMS_TYPE_FLOAT32:       p = "FLOAT32";    break;
  case SYMS_TYPE_FLOAT32PP:     p = "FLOAT32PP";  break;
  case SYMS_TYPE_FLOAT48:       p = "FLOAT48";    break;
  case SYMS_TYPE_FLOAT64:       p = "FLOAT64";    break;
  case SYMS_TYPE_FLOAT80:       p = "FLOAT80";    break;
  case SYMS_TYPE_FLOAT128:      p = "FLOAT128";   break;
  case SYMS_TYPE_CHAR:          p = "CHAR";       break;
  case SYMS_TYPE_VOID:          p = "VOID";       break;
  case SYMS_TYPE_BOOL:          p = "BOOL";       break;
  case SYMS_TYPE_PTR:           p = "PTR";        break;
  case SYMS_TYPE_ARR:           p = "ARR";        break;
  case SYMS_TYPE_ENUM:          p = "ENUM";       break;
  case SYMS_TYPE_PROC:          p = "PROC";       break;
  case SYMS_TYPE_PROC_PARAM:    p = "PROC_PARAM"; break;
  case SYMS_TYPE_TYPEDEF:       p = "TYPEDEF";    break;
  case SYMS_TYPE_STRUCT:        p = "STRUCT";     break;
  case SYMS_TYPE_UNION:         p = "UNION";      break;
  case SYMS_TYPE_CLASS:         p = "CLASS";      break;
  case SYMS_TYPE_METHOD:        p = "method";     break;
  case SYMS_TYPE_VIRTUAL_TABLE: p = "VIRTUAL_TABLE"; break;
  case SYMS_TYPE_BASE_CLASS:    p = "BASE CLASS";  break;
  case SYMS_TYPE_BITFIELD:      p = "BITFIELD";    break;
  case SYMS_TYPE_COMPLEX32:     p = "COMPLEX32";   break;
  case SYMS_TYPE_COMPLEX64:     p = "COMPLEX64";   break;
  case SYMS_TYPE_COMPLEX80:     p = "COMPLEX80";   break;
  case SYMS_TYPE_COMPLEX128:    p = "COMPLEX128";  break;
  case SYMS_TYPE_VARIADIC:      p = "VARIADIC";    break;
  case SYMS_TYPE_STRING:        p = "STRING";      break;
  case SYMS_TYPE_WCHAR:         p = "WCHAR";       break;
  }
  return p;
}

SYMS_API syms_uint
syms_get_type_kind_bitcount(SymsArch arch, SymsTypeKind kind)
{
  syms_uint result = 0;
  switch (kind) {
  case SYMS_TYPE_INT8:          result = 8;   break;
  case SYMS_TYPE_INT16:         result = 16;  break;
  case SYMS_TYPE_INT32:         result = 32;  break;
  case SYMS_TYPE_INT64:         result = 64;  break;
  case SYMS_TYPE_UINT8:         result = 8;   break;
  case SYMS_TYPE_UINT16:        result = 16;  break;
  case SYMS_TYPE_UINT32:        result = 32;  break;
  case SYMS_TYPE_UINT64:        result = 64;  break;
  case SYMS_TYPE_INT128:        result = 128; break;
  case SYMS_TYPE_INT256:        result = 256; break;
  case SYMS_TYPE_INT512:        result = 512; break;
  case SYMS_TYPE_UINT128:       result = 128; break;
  case SYMS_TYPE_UINT256:       result = 256; break;
  case SYMS_TYPE_UINT512:       result = 512; break;
  case SYMS_TYPE_FLOAT16:       result = 16;  break;
  case SYMS_TYPE_FLOAT32:       result = 32;  break;
  case SYMS_TYPE_FLOAT32PP:     result = 32;  break;
  case SYMS_TYPE_FLOAT48:       result = 48;  break;
  case SYMS_TYPE_FLOAT64:       result = 64;  break;
  case SYMS_TYPE_FLOAT80:       result = 80;  break;
  case SYMS_TYPE_FLOAT128:      result = 128; break;
  case SYMS_TYPE_CHAR:          result = 8;   break;
  case SYMS_TYPE_VOID:          result = syms_get_addr_size_ex(arch); break; 
  case SYMS_TYPE_TYPEDEF:       result = 0;   break;
  case SYMS_TYPE_STRUCT:        result = 0;   break;
  case SYMS_TYPE_UNION:         result = 0;   break;
  case SYMS_TYPE_ENUM:          result = 8;   break;
  case SYMS_TYPE_PROC:          result = syms_get_addr_size_ex(arch); break;
  case SYMS_TYPE_PROC_PARAM:    result = 0;   break;
  case SYMS_TYPE_ARR:           result = 0;   break;
  case SYMS_TYPE_PTR:           result = 0;   break;
  case SYMS_TYPE_BITFIELD:      result = 0;   break;
  case SYMS_TYPE_COMPLEX32:     result = 32;  break;
  case SYMS_TYPE_COMPLEX64:     result = 64;  break;
  case SYMS_TYPE_COMPLEX128:    result = 128; break;
  case SYMS_TYPE_VARIADIC:      result = 0;   break;
  case SYMS_TYPE_CLASS:         result = 0;   break;
  case SYMS_TYPE_METHOD:        result = 0;   break;
  case SYMS_TYPE_VIRTUAL_TABLE: result = 0;   break;
  case SYMS_TYPE_BASE_CLASS:    result = 0;   break;
  case SYMS_TYPE_BOOL:          result = 8;   break;
  case SYMS_TYPE_INVALID:       result = 0;   break;
  }
  SYMS_ASSERT(result % 8 == 0);
  return result;
}

SYMS_API syms_uint
syms_get_addr_size_ex(SymsArch arch)
{
  syms_uint addr_size = 0;

  switch (arch) {
  case SYMS_ARCH_NULL:  addr_size = 0; break;
  case SYMS_ARCH_X86:   addr_size = 4; break;
  case SYMS_ARCH_X64:   addr_size = 8; break;
  default: SYMS_INVALID_CODE_PATH;
  }
  
  return addr_size;
}

