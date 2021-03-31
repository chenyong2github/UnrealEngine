// Copyright Epic Games, Inc. All Rights Reserved.
/******************************************************************************
 * File   : syms_line_table.c                                                 *
 * Author : Nikita Smith                                                      *
 * Created: 2020/08/07                                                        *
 * Purpose: Contains routines for mapping address to source line and          *
 *          vice versa                                                        *
 ******************************************************************************/

SYMS_INTERNAL int
syms_qsort_addr_map_compar(const void *a, const void *b)
{
  const SymsAddrMap *map_a = (const SymsAddrMap *)a;
  const SymsAddrMap *map_b = (const SymsAddrMap *)b;
  if (map_a->addr < map_b->addr) return -1;
  if (map_a->addr > map_b->addr) return +1;
  // SYMS_ASSERT_FAILURE_PARANOID("address collision");
  return 0;
}

SYMS_INTERNAL SymsFileID
syms_line_table_push_file_(SymsLineTable *lt, SymsInstance *instance, SymsStringRef *name_ref, syms_bool register_file)
{
  SymsFileID result = SYMS_INVALID_FILE_ID;

  char temp[1024];
  char temp1[1024];

  if (lt->file_count > 0) {
	  syms_uint temp_size = syms_read_strref(instance, name_ref, temp, sizeof(temp));
	  SymsFileID file_hash = syms_hash_djb2(temp, temp_size) % SYMS_INVALID_FILE_ID;
	  SymsFileID best_id = file_hash % lt->file_count;
	  SymsFileID id = best_id;


	  do {
		  if (lt->files[id].type == SYMS_STRING_REF_TYPE_NULL) {
			  if (register_file) {
				  lt->files[id] = *name_ref;
			  }
			  result = id;
			  break;
		  }
		  syms_read_strref(instance, &lt->files[id], temp1, sizeof(temp1));
		  if (syms_strcmp(temp1, temp) == 0) {
			  result = id;
			  break;
		  }
		  id = (id + 1) % lt->file_count;
	  } while (id != best_id);
  }
  return result;
}

SYMS_INTERNAL SymsFileID
syms_line_table_push_file(SymsLineTable *lt, SymsInstance *instance, SymsStringRef *name_ref)
{
  return syms_line_table_push_file_(lt, instance, name_ref, syms_true);
}

SYMS_INTERNAL SymsFileID
syms_line_table_find_file(SymsLineTable *lt, SymsInstance *instance, SymsStringRef *name_ref)
{
  return syms_line_table_push_file_(lt, instance, name_ref, syms_false);
}

SYMS_INTERNAL SymsErrorCode
syms_line_table_build(SymsLineTable *lt, SymsInstance *instance, SymsMod *mod, SymsArena *arena)
{
  SymsLineIter line_iter;
  if (syms_line_iter_init(instance, &line_iter, mod)) {
    SymsLine line;
    syms_uint line_count = 0;
    syms_uint file_count = 0;
    SymsUMM block_size;
    u8 *block, *ptr;
    SymsFileID curr_file_id;

    while (syms_line_iter_next(&line_iter, &line)) {
      if (line_iter.switched_file) {
        ++file_count;
      }
      ++line_count;
    }

    block_size = line_count * (sizeof(lt->addrs[0]) + sizeof(lt->lines[0])) +
                 file_count * sizeof(lt->files[0]);
    if (block_size == 0) {
      return SYMS_ERR_OK; // no line table
    }
    block = syms_arena_push_array(arena, u8, block_size);
    if (block == NULL) {
      return SYMS_ERR_NOMEM;
    }
    ptr = block;
    lt->line_count = 0;
    lt->line_max = line_count;
    lt->file_count = file_count;
    lt->addrs = (SymsAddrMap *)ptr;
    ptr = (ptr + sizeof(lt->addrs[0]) * line_count);
    lt->lines = (SymsSourceMap *)ptr;
    ptr = (ptr + sizeof(lt->lines[0]) * line_count);
    lt->files = (SymsStringRef *)ptr;
    ptr = (ptr + sizeof(lt->files[0]) * file_count);
    // clear "files" array to zero, a zeroed entry is considered an empty
    // bucket
    syms_memset(lt->files, 0, file_count * sizeof(lt->files[0]));
    // make sure we exhausted allocated block
    SYMS_ASSERT(ptr == (void *)(block + block_size));
    curr_file_id = SYMS_INVALID_FILE_ID;
    syms_line_iter_init(instance, &line_iter, mod);
    while (syms_line_iter_next(&line_iter, &line)) {
      if (line_iter.switched_file) {
        curr_file_id = syms_line_table_push_file(lt, instance, &line_iter.file.name);
      }
      SYMS_ASSERT(lt->line_count < lt->line_max);
      lt->addrs[lt->line_count].addr = line.va;
      lt->addrs[lt->line_count].id = lt->line_count;
      lt->lines[lt->line_count].ln = line.ln;
      lt->lines[lt->line_count].col = line.col;
      lt->lines[lt->line_count].file = curr_file_id;
      lt->line_count += 1;
    }

    syms_qsort(lt->addrs, lt->line_count, sizeof(lt->addrs[0]), syms_qsort_addr_map_compar);
  }

  return SYMS_ERR_OK;
}

SYMS_API syms_bool
syms_line_table_map_va(SymsLineTable *lt, SymsAddr va, SymsSourceFileMap *map_out)
{
  SymsLineTableQuery query;
  if (syms_line_table_map_va_ex(lt, va, &query)) {
    *map_out = query.map;
    return syms_true;
  }
  return syms_false;
}

SYMS_API syms_bool
syms_line_table_map_va_ex(SymsLineTable *lt, SymsAddr va, SymsLineTableQuery *query_out)
{
  syms_int nearest_index = -1;
  SymsAddr nearest_delta = SYMS_ADDR_MAX;
  syms_bool is_va_mapped = syms_false;

  if (lt) {
    if (lt->line_count > 1) {
      if (va >= lt->addrs[0].addr && va <= lt->addrs[lt->line_count - 1].addr) {
        syms_int max = (S32)lt->line_count - 1;
        syms_int min = 0;
        syms_int mid = -1;
        while (min <= max) {
          mid = min + (max - min + 1)/2;
          if (va < lt->addrs[mid].addr) {
            max = mid - 1;
          } else if (va > lt->addrs[mid].addr) {
            SymsAddr delta = va - lt->addrs[mid].addr;
            if (delta < nearest_delta) {
              nearest_index = mid;
              nearest_delta = delta;
            }
            min = mid + 1;
          } else {
            nearest_index = mid;
            nearest_delta = 0;
            break;
          }
        }
      }
    } else if (lt->line_count == 1) {
      if (va >= lt->addrs[0].addr) {
        nearest_index = 0;
        nearest_delta = 0;
      }
    }

    if (nearest_index >= 0 && lt->line_count > 0) {
      syms_uint line_id;
      SymsSourceMap *src;

      SYMS_ASSERT((syms_uint)nearest_index < lt->line_count);

      line_id = lt->addrs[nearest_index].id; 
      src = &lt->lines[line_id];

      query_out->map.file.name        = lt->files[src->file];
      query_out->map.file.chksum_type = SYMS_CHECKSUM_NULL;
      query_out->map.line.va          = lt->addrs[nearest_index].addr;
      query_out->map.line.ln          = src->ln;
      query_out->map.line.col         = src->col;
      
      if ((syms_uint)nearest_index + 1 < lt->line_count) {
        SymsAddr a = lt->addrs[nearest_index + 0].addr;
        SymsAddr b = lt->addrs[nearest_index + 1].addr;
        query_out->map.instructions_size = (syms_uint)(b - a);
      } else {
        query_out->map.instructions_size = 0;
      }

      is_va_mapped = syms_true;
    }
  }

  return is_va_mapped;
}

SYMS_API syms_bool
syms_line_table_map_src(SymsLineTable *lt, SymsInstance *instance, SymsString filename, syms_uint ln, SymsSourceFileMap *map_out)
{
  SymsStringRef file_ref = syms_string_ref_str(filename);
  SymsFileID file = syms_line_table_find_file(lt, instance, &file_ref);
  syms_bool is_src_mapped = syms_false;
  if (file != SYMS_INVALID_FILE_ID) {
    syms_uint nearest_index = SYMS_UINT_MAX;
    syms_uint nearest_delta = SYMS_UINT_MAX;
    syms_uint i;
    for (i = 0; i < lt->line_count; ++i) {
      if (lt->lines[i].file == file) {
        syms_uint delta = (syms_uint)SYMS_ABS((syms_int)lt->lines[i].ln - (syms_int)ln);
        if (delta < nearest_delta) {
          nearest_index = i;
          nearest_delta = delta;
        }
      }
    }
    for (i = 0; i < lt->line_count; ++i) {
      if (lt->addrs[i].id == nearest_index) {
        map_out->file.name    = lt->files[file];
        map_out->file.chksum_type = SYMS_CHECKSUM_NULL;
        map_out->line.va      = lt->addrs[i].addr;
        map_out->line.ln      = lt->lines[nearest_index].ln;
        map_out->line.col     = lt->lines[nearest_index].col;

        if (i + 1 < lt->line_count) {
          map_out->instructions_size = (syms_uint)(lt->addrs[i + 1].addr - lt->addrs[i].addr);
        } else {
          map_out->instructions_size = 0;
        }

        is_src_mapped = syms_true;
        break;
      }
    }
  }
  return is_src_mapped;
}
