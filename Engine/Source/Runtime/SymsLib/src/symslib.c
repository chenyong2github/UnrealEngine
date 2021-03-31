// Copyright Epic Games, Inc. All Rights Reserved.
// symslib //

#include "symslib.h"


#include "regs/syms_regs.c"
#include "regs/syms_regs_intel.c"

#include "win/syms_nt.c"
#include "win/syms_nt_unwind.c"

#include "elf/syms_elf.c"

#include "pdb/syms_pdb.c"
#include "pdb/syms_pdb_api.c"

#include "dwarf/syms_dwarf.c"
#include "dwarf/syms_dwarf_api.c"
#include "dwarf/syms_dwarf_unwind.c"

#include "syms_virtual_unwind.c"
#include "syms_line_table.c"
#include "syms_block_alloc.c"
#include "syms.c"
#include "syms_enum.c"

#define SYMS_CORE_IMPLEMENT
#include "syms_core.h"

#if __APPLE__ || __linux__
#include "syms_macos.c"
#elif _WIN32
#include "syms_win32.c"
#else
#error undefined platform
#endif
