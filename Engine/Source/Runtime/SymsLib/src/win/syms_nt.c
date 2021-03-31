// Copyright Epic Games, Inc. All Rights Reserved.
/******************************************************************************
 * File		: syms_nt.c .																												*
 * Author : Nikita Smith																											*
 * Created: 2020/05/30																												*
 * Purpose: Implementation of routines for dealing with Portable Executables	*
 ******************************************************************************/

#include "syms_nt.h"

NT_API syms_bool
syms_img_init_nt(SymsImage *img, void *img_data, SymsUMM img_size, SymsLoadImageFlags load_flags)
{
	SymsImageNT *nt = (SymsImageNT *)img->impl;
	SymsBuffer img_read = syms_buffer_init(img_data, img_size);
	SymsNTDataDir *debug_dir = 0;

	img->type = SYMS_IMAGE_NULL;
	syms_memzero(nt, sizeof(*nt));

	nt->dos_header = syms_buffer_push_struct(&img_read, SymsDosHeader);
	if (nt->dos_header && nt->dos_header->e_magic == SYMS_DOS_MAGIC) {
		if (nt->dos_header->e_lfanew >= (S32)sizeof(SymsDosHeader)) {
      if (syms_buffer_seek(&img_read, (U32)nt->dos_header->e_lfanew)) {
        SymsImageHeaderClass header_class = SYMS_IMAGE_HEADER_CLASS_NULL;
        u32 *sig = syms_buffer_push_struct(&img_read, u32);
        if (sig != 0 && *sig == SYMS_NT_FILE_HEADER_SIG) {
          nt->file_header = syms_buffer_push_struct(&img_read, SymsNTFileHeader);
          if (nt->file_header) {
            switch (nt->file_header->machine) {
            default: SYMS_ASSERT_FAILURE("unsupported machine"); break;

                     // where should EFI Byte Code go?
            case SYMS_NT_FILE_HEADER_MACHINE_EBC: break;

            case SYMS_NT_FILE_HEADER_MACHINE_WCEMIPSV2:
            case SYMS_NT_FILE_HEADER_MACHINE_THUMB:
            case SYMS_NT_FILE_HEADER_MACHINE_SH4:
            case SYMS_NT_FILE_HEADER_MACHINE_SH3DSP:
            case SYMS_NT_FILE_HEADER_MACHINE_SH3:
            case SYMS_NT_FILE_HEADER_MACHINE_RISCV32:
            case SYMS_NT_FILE_HEADER_MACHINE_POWERPC:
            case SYMS_NT_FILE_HEADER_MACHINE_POWERPCFP:
            case SYMS_NT_FILE_HEADER_MACHINE_MIPS16:
            case SYMS_NT_FILE_HEADER_MACHINE_MIPSFPU:
            case SYMS_NT_FILE_HEADER_MACHINE_MIPSFPU16:
            case SYMS_NT_FILE_HEADER_MACHINE_M32R:
            case SYMS_NT_FILE_HEADER_MACHINE_I386:
            case SYMS_NT_FILE_HEADER_MACHINE_ARMNT:
            case SYMS_NT_FILE_HEADER_MACHINE_ARM:
            case SYMS_NT_FILE_HEADER_MACHINE_AM33: {
              header_class = SYMS_IMAGE_HEADER_CLASS_32; 
            } break;

            case SYMS_NT_FILE_HEADER_MACHINE_SH5:
            case SYMS_NT_FILE_HEADER_MACHINE_RISCV128:
            case SYMS_NT_FILE_HEADER_MACHINE_RISCV64:
            case SYMS_NT_FILE_HEADER_MACHINE_R4000:
            case SYMS_NT_FILE_HEADER_MACHINE_IA64:
            case SYMS_NT_FILE_HEADER_MACHINE_ARM64:
            case SYMS_NT_FILE_HEADER_MACHINE_X64: {
              header_class = SYMS_IMAGE_HEADER_CLASS_64; 
            } break;
            }
          }

          switch (header_class) {
          case SYMS_IMAGE_HEADER_CLASS_NULL: break;
          case SYMS_IMAGE_HEADER_CLASS_32: {
            nt->u.header32 = syms_buffer_push_struct(&img_read, SymsNTOptionalHeader32);
            if (nt->u.header32) {
              img->type					= SYMS_IMAGE_NT;
              img->header_class = SYMS_IMAGE_HEADER_CLASS_32;
              img->arch					= SYMS_ARCH_X86;
              img->base_addr		= (SymsAddr)nt->u.header64->image_base;

              debug_dir = &nt->u.header32->dirs[SYMS_NT_DATA_DIR_DEBUG];
            }
          } break;
          case SYMS_IMAGE_HEADER_CLASS_64: {
            nt->u.header64 = syms_buffer_push_struct(&img_read, SymsNTOptionalHeader64);
            if (nt->u.header64) {
              img->type					= SYMS_IMAGE_NT;
              img->header_class = SYMS_IMAGE_HEADER_CLASS_64;
              img->arch					= SYMS_ARCH_X64;
              img->base_addr		= nt->u.header64->image_base;

              debug_dir = &nt->u.header64->dirs[SYMS_NT_DATA_DIR_DEBUG];
            }
          } break;
          }
        }
      }
		}
	}
	if (img->type == SYMS_IMAGE_NULL) {
		return syms_false;
	}
		
	// NOTE(nick): Extracting pdb path and info for checking validity of pdb like GUID, age, time
	if (debug_dir) {
		SymsOffset debug_data_offset = debug_dir->rva; 
		if (~load_flags & SYMS_LOAD_IMAGE_FLAGS_FROM_MEMORY) {
			SymsSecIterNT it = syms_sec_iter_init_nt(img);
			SymsNTImageSectionHeader sec;
			while (syms_sec_iter_next_nt(&it, &sec)) {
				if (debug_dir->rva >= sec.va && debug_dir->rva < sec.va + sec.sizeof_rawdata) {
					debug_data_offset = sec.rawdata_ptr + (debug_dir->rva - sec.va);
					break;
				}
			}
		}

		if (syms_buffer_seek(&img_read, debug_data_offset)) {
			SymsNTDebugDir *debug_data = syms_buffer_push_struct(&img_read, SymsNTDebugDir);
			if (debug_data) {
				switch (debug_data->type) {
				case SYMS_NT_DEBUG_DIR_CODEVIEW: {
					u32 sig;
					SymsAddr cv_data_off = (load_flags & SYMS_LOAD_IMAGE_FLAGS_FROM_MEMORY) ? debug_data->raw_data_rva : debug_data->raw_data_ptr;
					if (!syms_buffer_seek(&img_read, cv_data_off)) {
						break;
					}
					sig = syms_buffer_peek_u32(&img_read);
					switch (sig) {
					case SYMS_CODEVIEW_SIG_V410:
					case SYMS_CODEVIEW_SIG_V500: {
					} break;
					case SYMS_CODEVIEW_SIG_PDB20: {
						SymsCodeViewHeaderPDB20 *cv = syms_buffer_push_struct(&img_read, SymsCodeViewHeaderPDB20);
						if (cv) {
							nt->pdb_age = cv->age;
							nt->pdb_time = cv->time;
							nt->pdb_path = syms_buffer_read_string(&img_read);
						}
					} break;
					case SYMS_CODEVIEW_SIG_PDB70: {
						SymsCodeViewHeaderPDB70 *cv = syms_buffer_push_struct(&img_read, SymsCodeViewHeaderPDB70);
						if (cv) {
							nt->pdb_age = cv->age;
							nt->pdb_time = 0;
							syms_memcpy(&nt->pdb_guid, &cv->guid, sizeof(cv->guid));
							nt->pdb_path = syms_buffer_read_string(&img_read);
						}
					} break;
					}
				} break;
				case SYMS_NT_DEBUG_DIR_COFF:
				case SYMS_NT_DEBUG_DIR_FPO:
				case SYMS_NT_DEBUG_DIR_MISC:
				case SYMS_NT_DEBUG_DIR_UNKNOWN:
				default: {
				} break;
				}
			}
		}
	}
	
	return syms_true;
}

SYMS_INTERNAL SymsSecIterNT
syms_sec_iter_init_nt(SymsImage *img) 
{
	SymsImageNT *nt = (SymsImageNT *)img->impl;
	SymsSecIterNT result;
	SymsAddr secs_lo, secs_hi;
	result.header_index = 0;
	result.header_count = 0;
	result.headers = 0;
	secs_lo = secs_hi = 0;
	switch (img->header_class) {
	case SYMS_IMAGE_HEADER_CLASS_32: secs_lo = sizeof(SymsNTOptionalHeader32); break;
	case SYMS_IMAGE_HEADER_CLASS_64: secs_lo = sizeof(SymsNTOptionalHeader64); break;
	default: return result;
	}
	secs_lo += (U32)nt->dos_header->e_lfanew + sizeof(SymsNTFileHeader) + sizeof(u32);
	secs_hi = secs_lo + nt->file_header->number_of_sections * sizeof(SymsNTImageSectionHeader);
	if (secs_hi <= img->img_data_size) {
		void *secs_data;

		result.img = img;
		result.header_count = nt->file_header->number_of_sections;
		secs_data = (void *)((U8 *)img->img_data + secs_lo);
		result.headers = (SymsNTImageSectionHeader *)secs_data;
	}
	return result;
}

SYMS_INTERNAL syms_bool
syms_sec_iter_next_nt(SymsSecIterNT *iter, SymsNTImageSectionHeader *sec_out)
{
	syms_bool result = syms_false;
	syms_memzero(sec_out, sizeof(*sec_out));
	if (iter->header_index < iter->header_count) {
    *sec_out = iter->headers[iter->header_index]; 
    iter->header_index += 1;
		result = syms_true;
	}
	return result;
}

#if 0
SYMS_API SymsAddr
syms_img_find_procedure(SymsImgInfo *img, const char *procedure_name)
{
	SymsAddr result = 0;

	switch (img->type) {
	case SYMS_IMAGE_NT: {
		char *name;
		int name_len;

		int min, max;

		int name_index;

		uintptr_t read_va;
		char memchunk[128];

		if (!C_STRING_IS_ASCII(name_str)) {
			return 0;
		}

		name = (char *)name_str.ptr;
		name_len = string_get_count(name_str);

		min = 0;
		max = mod->export_table.num_names - 1;
		name_index = -1;

		ASSERT(name_len < (int)sizeof(memchunk));

		if (~mod->flags & MODULE_FLAG_HAS_EXPORT_TABLE) {
			/* Nothing in the export table. */
			return 0;
		}

		while (min <= max) {
			int i;
			int mid = (min + max) / 2;
			int cmp;
			u32 name_rva;

			read_va = (uintptr_t)mod->base;
			read_va += mod->export_table.names_rva + sizeof(u32)*mid;
			if (!win64_tracer_memread(tracer, read_va, &name_rva, sizeof(u32))) {
				break;
			}

			read_va = (uintptr_t)mod->base + name_rva;
			if (!win64_tracer_memread(tracer, read_va, memchunk, sizeof(memchunk))) {
				goto exit;
			}

			i = 0;
			while (name[i] == memchunk[i]) {
				if (name[i] == '\0' || memchunk[i] == '\0') {
					--i;
					break;
				}

				++i;
			}

			cmp = c_string_compare(memchunk, name);
			if (cmp < 0) {
				min = mid + 1;
			} else if (cmp > 0) {
				max = mid - 1;
			} else if (cmp == 0) {
				name_index = mid;
				break;
			}
		}

		if (name_index != -1) {
			u16 ordinal;
			u32 func_rva;
			extern_ptr func_va;

			STATIC_ASSERT(sizeof(IMAGE_EXPORT_DIRECTORY) == sizeof(nt_image_export_table_t));

			read_va = (uintptr_t)mod->base;
			read_va += mod->export_table.ordinals_rva + name_index * sizeof(u16);
			if (!win64_tracer_memread(tracer, read_va, &ordinal, sizeof(u16))) {
				goto exit;
			}

			if (ordinal >= mod->export_table.num_funcs) {
				goto exit;
			}

			ordinal = c_truncU16(MAX(ordinal - (mod->export_table.base - 1), 0));

			read_va = mod->base;
			read_va += mod->export_table.funcs_rva + ordinal * sizeof(u32);
			if (!win64_tracer_memread(tracer, read_va, &func_rva, sizeof(u32))) {
				goto exit;
			}

			func_va = (mod->base + func_rva);

			return func_va;
		}

exit:
		return 0;
	} break;

	case SYMS_IMAGE_ELF: {
		SYMS_ASSERT_FAILURE("syms: elf header parsing is not implemented yet");
	} break;

	default: {
		SYMS_ASSERT_FAILURE_ALWAYS("syms: invalid img type");
	} break;
	}
}
#endif

SYMS_INTERNAL SymsNTPdata
syms_unpack_pdata(struct SymsInstance *instance, SymsNTPdataPacked *pdata)
{
	SymsNTPdata result;
	SymsAddr rebase = syms_get_rebase(instance);
	result.lo			= rebase + pdata->rva_lo;
	result.hi			= rebase + pdata->rva_hi;
	result.uwinfo = rebase + pdata->uw_info_rva;
	return result;
}

SYMS_INTERNAL SymsErrorCode
syms_find_nearest_pdata(struct SymsInstance *instance, SymsAddr ip, SymsNTPdata *pdata_out)
{
	SymsImageNT *nt = (SymsImageNT *)instance->img.impl;
	SymsSection pdata_sec;
	SymsErrorCode result = SYMS_ERR_INVALID_CODE_PATH;

	SYMS_ASSERT(instance->img.type == SYMS_IMAGE_NT);

	if (syms_img_sec_from_name(instance, syms_string_lit(".pdata"), &pdata_sec)) {
		SymsAddr rebase = syms_get_rebase(instance);
		SymsAddr rva = (ip - rebase);

		if (!nt->pdata_count) {
			nt->pdata_count = (U32)(pdata_sec.data_size / sizeof(SymsNTPdataPacked));
			while (nt->pdata_count > 0) {
				SymsNTPdataPacked *entry = (SymsNTPdataPacked *)pdata_sec.data + (nt->pdata_count - 1);
				if (entry->rva_lo != 0) {
					SYMS_ASSERT(entry->rva_hi != 0);
					break;
				}
				--nt->pdata_count;
			}
		}

		if (nt->pdata_count > 0) {
			U32 min = 0;
			U32 max = nt->pdata_count - 1;

			while (min <= max) {
				U32 mid = (min + max)/2;
				SymsNTPdataPacked *entry = (SymsNTPdataPacked *)pdata_sec.data + mid;

				SYMS_ASSERT(entry->rva_lo <= entry->rva_hi);
				if (rva < entry->rva_lo) {
					max = mid - 1;
				} else if (rva >= entry->rva_hi) { 
					min = mid + 1;
				} else {
					while (entry->uw_info_rva & 1) {
						mid = (entry->uw_info_rva & ~1u)/sizeof(SymsNTPdataPacked);
						if (mid >= nt->pdata_count) {
							return SYMS_ERR_INVALID_CODE_PATH;
						}
					}
					*pdata_out = syms_unpack_pdata(instance, ((SymsNTPdataPacked *)pdata_sec.data + mid));
					result = SYMS_ERR_OK;
					break;
				}
			}
		}
	}

	return result;
}

SYMS_INTERNAL SymsAddr
syms_get_rebase_nt(const SymsImageNT *nt, SymsImageHeaderClass header_class, SymsAddr base)
{ (void)nt; (void)header_class;
	return base;
}

NT_API const char *
syms_get_nt_machine_str(u32 machine)
{
	const char *machine_str = 0;
	switch (machine) {
	case SYMS_NT_FILE_HEADER_MACHINE_UNKNOWN:			machine_str = "Unknown";											break;
	case SYMS_NT_FILE_HEADER_MACHINE_X86:					machine_str = "x86";													break;
	case SYMS_NT_FILE_HEADER_MACHINE_X64:					machine_str = "x64";													break;
	case SYMS_NT_FILE_HEADER_MACHINE_AM33:				machine_str = "Matsushita AM33";							break;
	case SYMS_NT_FILE_HEADER_MACHINE_ARM:					machine_str = "ARM";													break;
	case SYMS_NT_FILE_HEADER_MACHINE_ARM64:				machine_str = "ARM (64bit)";									break;
	case SYMS_NT_FILE_HEADER_MACHINE_ARMNT:				machine_str = "ARM (NT)";											break;
	case SYMS_NT_FILE_HEADER_MACHINE_EBC:					machine_str = "ECB";													break;
	case SYMS_NT_FILE_HEADER_MACHINE_IA64:				machine_str = "IA64";													break;
	case SYMS_NT_FILE_HEADER_MACHINE_M32R:				machine_str = "M32R";													break;
	case SYMS_NT_FILE_HEADER_MACHINE_MIPS16:			machine_str = "MIPS (16bit)";									break;
	case SYMS_NT_FILE_HEADER_MACHINE_MIPSFPU:			machine_str = "MIPS (with FPU)";							break;
	case SYMS_NT_FILE_HEADER_MACHINE_MIPSFPU16:		machine_str = "MIPS (16bit with FPU)";				break;
	case SYMS_NT_FILE_HEADER_MACHINE_POWERPC:			machine_str = "PowerPC (little-endian)";			break;
	case SYMS_NT_FILE_HEADER_MACHINE_POWERPCFP:		machine_str = "PowerPC (with float support)"; break;
	case SYMS_NT_FILE_HEADER_MACHINE_R4000:				machine_str = "R4000";												break;
	case SYMS_NT_FILE_HEADER_MACHINE_RISCV32:			machine_str = "RISCV32";											break;
	case SYMS_NT_FILE_HEADER_MACHINE_RISCV64:			machine_str = "RISCV64";											break;
	case SYMS_NT_FILE_HEADER_MACHINE_RISCV128:		machine_str = "RISCV128";											break;
	case SYMS_NT_FILE_HEADER_MACHINE_SH3:					machine_str = "Hitachi SH3";									break;
	case SYMS_NT_FILE_HEADER_MACHINE_SH3DSP:			machine_str = "Hitachi SH3 DPS";							break;
	case SYMS_NT_FILE_HEADER_MACHINE_SH4:					machine_str = "Hitachi SH4";									break;
	case SYMS_NT_FILE_HEADER_MACHINE_SH5:					machine_str = "Hitachi Sh5";									break;
	case SYMS_NT_FILE_HEADER_MACHINE_THUMB:				machine_str = "Thumb";												break;
	case SYMS_NT_FILE_HEADER_MACHINE_WCEMIPSV2:		machine_str = "MIPS (little-endian WCE v2)";	break;
	}
	return machine_str;
}
