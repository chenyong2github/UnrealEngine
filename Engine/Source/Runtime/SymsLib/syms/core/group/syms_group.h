// Copyright Epic Games, Inc. All Rights Reserved.
/* date = August 2nd 2021 0:48 pm */

#ifndef SYMS_GROUP_H
#define SYMS_GROUP_H

////////////////////////////////
// NOTE(allen): Syms Type Graph

typedef struct SYMS_TypeMember{
  SYMS_MemKind kind;
  SYMS_MemVisibility visibility;
  SYMS_MemFlags flags;
  SYMS_String8 name;
  SYMS_U32 off;
  SYMS_U32 virtual_off;
  struct SYMS_TypeNode *type;
} SYMS_TypeMember;

typedef struct SYMS_TypeSrcCoord{
  SYMS_USID usid;
  SYMS_FileID file_id;
  SYMS_U32 line;
  SYMS_U32 col;
} SYMS_TypeSrcCoord;

typedef struct SYMS_TypeMemberArray{
  SYMS_TypeMember *mems;
  SYMS_U64 count;
} SYMS_TypeMemberArray;

typedef struct SYMS_TypeNode{
  // SYMS_TypeNode extends and completes the information from SYMS_TypeInfo.
  // See SYMS_TypeInfo for more interpretation info.
  
  SYMS_TypeKind kind;
  SYMS_String8 name;
  SYMS_U64 byte_size;
  
  // when non-null contains the source location of the type's definition
  SYMS_TypeSrcCoord *src_coord;
  
  // (in addition to interpretations of SYMS_TypeInfo 'direct_type')
  //  SYMS_TypeKind_Forward*   -> the concrete type referenced by the forward reference
  struct SYMS_TypeNode *direct_type;
  
  // 'this_type' meaning depends on kind:
  //  SYMS_TypeKind_MemberPtr  -> the container type of the member pointer
  //  SYMS_TypeKind_Proc       -> if non-nil this is the type of an implicit 'this' in a C++ method
  struct SYMS_TypeNode *this_type;
  
  union{
    // kind: SYMS_TypeKind_Modifier
    SYMS_TypeModifiers mods;
    
    // kind: SYMS_TypeKind_Array
    SYMS_U64 array_count;
    
    // kind: SYMS_TypeKind_Proc
    struct{
      struct SYMS_TypeNode **params;
      SYMS_U64 param_count;
    } proc;
    
    // opaque pointer for lazy eval attachments to the type node.
    void *lazy_ptr;
  };
} SYMS_TypeNode;

typedef struct SYMS_TypeUSIDNode{
  struct SYMS_TypeUSIDNode *next;
  SYMS_USID key;
  SYMS_TypeNode *type;
} SYMS_TypeUSIDNode;

typedef struct SYMS_TypeUSIDBuckets{
  SYMS_TypeUSIDNode **buckets;
  SYMS_U64 bucket_count;
} SYMS_TypeUSIDBuckets;

typedef struct SYMS_TypeContentNode{
  struct SYMS_TypeContentNode *next;
  SYMS_String8 key;
  SYMS_U64 hash;
  SYMS_TypeNode *type;
} SYMS_TypeContentNode;

typedef struct SYMS_TypeContentBuckets{
  SYMS_TypeContentNode **buckets;
  SYMS_U64 bucket_count;
} SYMS_TypeContentBuckets;

////////////////////////////////
// NOTE(allen): Syms Line Mapping Structures

typedef struct SYMS_LineToAddrMap{
  SYMS_U64Range *ranges;
  // line_range_indexes ranges from [0,line_count] inclusive so that:
  // for-all i in [0,line_count):
  //   (line_range_indexes[i + 1] - line_range_indexes[i]) == # of ranges for line at index i
  SYMS_U32 *line_range_indexes;
  SYMS_U32 *line_numbers;
  SYMS_U64 line_count;
} SYMS_LineToAddrMap;

typedef struct SYMS_FileToLineToAddrNode{
  struct SYMS_FileToLineToAddrNode *next;
  SYMS_FileID file_id;
  SYMS_LineToAddrMap *map;
} SYMS_FileToLineToAddrNode;

typedef struct SYMS_FileToLineToAddrBuckets{
  SYMS_FileToLineToAddrNode **buckets;
  SYMS_U64 bucket_count;
} SYMS_FileToLineToAddrBuckets;

//- loose version
typedef struct SYMS_FileToLineToAddrLooseLine{
  struct SYMS_FileToLineToAddrLooseLine *next;
  SYMS_U32 line;
  SYMS_U64RangeList ranges;
} SYMS_FileToLineToAddrLooseLine;

typedef struct SYMS_FileToLineToAddrLooseFile{
  struct SYMS_FileToLineToAddrLooseFile *next;
  SYMS_FileID file_id;
  SYMS_FileToLineToAddrLooseLine *first;
  SYMS_FileToLineToAddrLooseLine *last;
  SYMS_U64 line_count;
  SYMS_U64 range_count;
} SYMS_FileToLineToAddrLooseFile;

typedef struct SYMS_FileToLineToAddrLoose{
  SYMS_FileToLineToAddrLooseFile *first;
  SYMS_FileToLineToAddrLooseFile *last;
  SYMS_U64 count;
} SYMS_FileToLineToAddrLoose;

////////////////////////////////
// NOTE(allen): Syms Group Types

typedef SYMS_U32 SYMS_GroupUnitCacheFlags;
enum{
  SYMS_GroupUnitCacheFlag_HasProcSidArray  = (1 << 0),
  SYMS_GroupUnitCacheFlag_HasVarSidArray   = (1 << 1),
  SYMS_GroupUnitCacheFlag_HasTypeSidArray  = (1 << 2),
  SYMS_GroupUnitCacheFlag_HasFileTable     = (1 << 3),
  SYMS_GroupUnitCacheFlag_HasInfFileTable  = (1 << 4),
  SYMS_GroupUnitCacheFlag_HasLineTable     = (1 << 5),
  SYMS_GroupUnitCacheFlag_HasProcMap       = (1 << 6),
  SYMS_GroupUnitCacheFlag_HasVarMap        = (1 << 7),
  SYMS_GroupUnitCacheFlag_HasLineSeqMap    = (1 << 8),
  SYMS_GroupUnitCacheFlag_HasLineToAddrMap = (1 << 9),
};

typedef struct SYMS_Group{
  SYMS_Arena *arena;
  
  //- thread lanes
  SYMS_Arena **lane_arenas;
  SYMS_U64 lane_count;
  SYMS_U64 lane_max;
  
  //- data for binary and debug files
  SYMS_String8 bin_data;
  SYMS_String8 dbg_data;
  SYMS_BinAccel *bin;
  SYMS_DbgAccel *dbg;
  
  //- top-level accelerators and info
  SYMS_Arch arch;
  SYMS_U64 address_size;
  SYMS_U64 default_vbase;
  SYMS_SecInfoArray sec_info_array;
  SYMS_UnitSetAccel *unit_set;
  SYMS_U64 unit_count;
  SYMS_MapAccel *type_map;
  
  //- basic section caches
  SYMS_String8 *sec_names;
  
  //- basic unit caches
  SYMS_GroupUnitCacheFlags *unit_cache_flags;
  SYMS_UnitAccel **units;
  SYMS_SymbolIDArray *proc_sid_arrays;
  SYMS_SymbolIDArray *var_sid_arrays;
  SYMS_SymbolIDArray *type_sid_arrays;
  SYMS_String8Array *file_tables;
  SYMS_String8Array *inferred_file_tables;
  SYMS_LineParseOut *line_tables;
  SYMS_SpatialMap1D *unit_proc_maps;
  SYMS_SpatialMap1D *unit_var_maps;
  SYMS_SpatialMap1D *line_sequence_maps;
  SYMS_FileToLineToAddrBuckets *file_to_line_to_addr_buckets;
  
  //- hash tables caches
  SYMS_StringCons string_cons;
  SYMS_FileID2NameMap file_id_2_name_map;
  SYMS_TypeUSIDBuckets type_usid_buckets;
  SYMS_TypeContentBuckets type_content_buckets;
  
  //- one-time fills/builds
  SYMS_B8 unit_ranges_is_filled;
  SYMS_B8 type_map_unit_is_filled;
  SYMS_B8 sec_map_v_is_built;
  SYMS_B8 sec_map_f_is_built;
  SYMS_B8 unit_map_is_built;
  SYMS_B8 name_2_file_id_map_is_built;
  SYMS_B8 stripped_info_is_filled;
  SYMS_B8 stripped_info_map_is_built;
  
  SYMS_UnitRangeArray unit_ranges;
  SYMS_UnitAccel *type_map_unit;
  SYMS_SpatialMap1D sec_map_v;
  SYMS_SpatialMap1D sec_map_f;
  SYMS_SpatialMap1D unit_map;
  SYMS_Name2FileIDMap name_2_file_id_map;
  SYMS_StrippedInfoArray stripped_info;
  SYMS_SpatialMap1D stripped_info_map;
} SYMS_Group;

////////////////////////////////
// NOTE(allen): Data Structure Nils

SYMS_READ_ONLY SYMS_GLOBAL SYMS_SymbolIDArray syms_sid_array_nil = {0};
SYMS_READ_ONLY SYMS_GLOBAL SYMS_String8Array syms_string_array_nil = {0};
SYMS_READ_ONLY SYMS_GLOBAL SYMS_LineParseOut syms_line_parse_nil = {0};
SYMS_READ_ONLY SYMS_GLOBAL SYMS_SpatialMap1D syms_spatial_map_1d_nil = {0};
SYMS_READ_ONLY SYMS_GLOBAL SYMS_LineToAddrMap syms_line_to_addr_map_nil = {0};
SYMS_READ_ONLY SYMS_GLOBAL SYMS_TypeMemberArray syms_type_member_array_nil = {0};
SYMS_READ_ONLY SYMS_GLOBAL SYMS_EnumInfoArray syms_enum_info_array_nil = {0};

SYMS_READ_ONLY SYMS_GLOBAL SYMS_TypeNode syms_type_graph_nil = {
  SYMS_TypeKind_Null,     // kind
  {(SYMS_U8*)"(nil)", 5}, // name
  0,                      // byte_size
  0,                      // src_coord
  &syms_type_graph_nil,   // direct_type
  &syms_type_graph_nil,   // this_type
};


////////////////////////////////
// NOTE(allen): File Inference Helper Macros

// TODO(allen): Just here to help people with transition. Eventually we're
// going to cut the helper macros just have syms_file_inf_infer*
#define SYMS_GroupInitParams            SYMS_ParseBundle
#define syms_group_infer_from_file_list syms_file_inf_infer_from_file_list
#define syms_group_infer_from_file      syms_file_inf_infer_from_file

////////////////////////////////
// NOTE(allen): Syms Group Setup Functions

SYMS_API SYMS_Group* syms_group_alloc(void);
SYMS_API void        syms_group_release(SYMS_Group *group);

SYMS_API void        syms_group_init(SYMS_Group *group, SYMS_ParseBundle *params);

SYMS_API void        syms_group_parse_all_units__single_thread(SYMS_Group *group);
SYMS_API void        syms_group_parse_all_top_level(SYMS_Group *group);

SYMS_API void        syms_group_begin_multilane(SYMS_Group *group, SYMS_U64 lane_count);
SYMS_API void        syms_group_end_multilane(SYMS_Group *group);

SYMS_API SYMS_Arena* syms_group_get_lane_arena(SYMS_Group *group);

////////////////////////////////
// NOTE(allen): Syms Group Getters & Cache Accessors

// TODO(allen): better sorting plan here.

SYMS_API SYMS_String8       syms_group_bin_data(SYMS_Group *group);
SYMS_API SYMS_BinAccel*     syms_group_bin(SYMS_Group *group);
SYMS_API SYMS_String8       syms_group_dbg_data(SYMS_Group *group);
SYMS_API SYMS_DbgAccel*     syms_group_dbg(SYMS_Group *group);
SYMS_API SYMS_UnitSetAccel* syms_group_unit_set(SYMS_Group *group);

SYMS_API SYMS_U64           syms_group_address_size(SYMS_Group *group);
SYMS_API SYMS_U64           syms_group_default_vbase(SYMS_Group *group);

SYMS_API SYMS_SecInfoArray  syms_group_sec_info_array(SYMS_Group *group);
SYMS_API SYMS_SecInfo*      syms_group_sec_info_from_number(SYMS_Group *group, SYMS_U64 n);

SYMS_API SYMS_U64           syms_group_unit_count(SYMS_Group *group);
SYMS_API SYMS_UnitInfo      syms_group_unit_info_from_uid(SYMS_Group *group, SYMS_UnitID uid);
SYMS_API SYMS_UnitNames     syms_group_unit_names_from_uid(SYMS_Arena *arena, SYMS_Group *group, SYMS_UnitID uid);

// thread safe (with lanes equipped to group)
SYMS_API SYMS_UnitAccel*    syms_group_unit_from_uid(SYMS_Group *group, SYMS_UnitID uid);
SYMS_API SYMS_SymbolIDArray*syms_group_proc_sid_array_from_uid(SYMS_Group *group, SYMS_UnitID uid);
SYMS_API SYMS_SymbolIDArray*syms_group_var_sid_array_from_uid(SYMS_Group *group, SYMS_UnitID uid);
SYMS_API SYMS_SymbolIDArray*syms_group_type_sid_array_from_uid(SYMS_Group *group, SYMS_UnitID uid);
SYMS_API SYMS_String8Array* syms_group_file_table_from_uid(SYMS_Group *group, SYMS_UnitID uid);
SYMS_API SYMS_LineParseOut* syms_group_line_parse_from_uid(SYMS_Group *group, SYMS_UnitID uid);
SYMS_API SYMS_LineTable*    syms_group_line_table_from_uid(SYMS_Group *group, SYMS_UnitID uid);
SYMS_API SYMS_String8Array* syms_group_inferred_file_table_from_uid(SYMS_Group *group, SYMS_UnitID uid);

SYMS_API SYMS_String8Array* syms_group_file_table_from_uid_with_fallbacks(SYMS_Group *group, SYMS_UnitID uid);

SYMS_API SYMS_UnitRangeArray syms_group_unit_ranges(SYMS_Group *group);

SYMS_API SYMS_SymbolKind    syms_group_symbol_kind_from_sid(SYMS_Group *group,
                                                            SYMS_UnitAccel *unit, SYMS_SymbolID sid);
SYMS_API SYMS_String8       syms_group_symbol_name_from_sid(SYMS_Arena *arena, SYMS_Group *group,
                                                            SYMS_UnitAccel *unit, SYMS_SymbolID sid);

SYMS_API SYMS_String8       syms_group_file_name_from_id(SYMS_Group *group, SYMS_UnitID uid, SYMS_FileID file_id);

SYMS_API SYMS_MapAndUnit    syms_group_type_map(SYMS_Group *group);

SYMS_API SYMS_StrippedInfoArray syms_group_stripped_info(SYMS_Group *group);


////////////////////////////////
// NOTE(allen): Syms Group Address Mapping Functions

//- linear scan versions
SYMS_API SYMS_U64      syms_group_sec_number_from_voff__linear_scan(SYMS_Group *group, SYMS_U64 voff);
SYMS_API SYMS_U64      syms_group_sec_number_from_foff__linear_scan(SYMS_Group *group, SYMS_U64 foff);

SYMS_API SYMS_U64Maybe syms_group_voff_from_foff__linear_scan(SYMS_Group *group, SYMS_U64 foff);
SYMS_API SYMS_U64Maybe syms_group_foff_from_voff__linear_scan(SYMS_Group *group, SYMS_U64 voff);

SYMS_API SYMS_UnitID   syms_group_uid_from_voff__linear_scan(SYMS_Group *group, SYMS_U64 voff);
SYMS_API SYMS_SymbolID syms_group_proc_sid_from_uid_voff__linear_scan(SYMS_Group *group, SYMS_UnitID uid,
                                                                      SYMS_U64 voff);
SYMS_API SYMS_SymbolID syms_group_var_sid_from_uid_voff__linear_scan(SYMS_Group *group, SYMS_UnitID uid,
                                                                     SYMS_U64 voff);

SYMS_API SYMS_Line     syms_group_line_from_uid_voff__linear_scan(SYMS_Group *group, SYMS_UnitID uid,
                                                                  SYMS_U64 voff);
SYMS_API SYMS_U64RangeList syms_group_vranges_from_uid_line__linear_scan(SYMS_Arena *arena, SYMS_Group *group,
                                                                         SYMS_UnitID uid,
                                                                         SYMS_FileID file_id, SYMS_U32 line);

//- map getters
SYMS_API SYMS_SpatialMap1D* syms_group_sec_map_v(SYMS_Group *group);
SYMS_API SYMS_SpatialMap1D* syms_group_sec_map_f(SYMS_Group *group);

SYMS_API SYMS_SpatialMap1D* syms_group_unit_map(SYMS_Group *group);

// thread safe (with lanes equipped to group)
SYMS_API SYMS_SpatialMap1D* syms_group_proc_map_from_uid(SYMS_Group *group, SYMS_UnitID uid);
SYMS_API SYMS_SpatialMap1D* syms_group_line_sequence_map_from_uid(SYMS_Group *group, SYMS_UnitID uid);

SYMS_API SYMS_SpatialMap1D* syms_group_var_map_from_uid(SYMS_Group *group, SYMS_UnitID uid);

SYMS_API void                syms_group_fetch_line_to_addr_maps_from_uid(SYMS_Group *group, SYMS_UnitID uid);
SYMS_API SYMS_LineToAddrMap* syms_group_line_to_addr_map_from_uid_file_id(SYMS_Group *group, SYMS_UnitID uid,
                                                                          SYMS_FileID file_id);

SYMS_API SYMS_SpatialMap1D* syms_group_stripped_info_map(SYMS_Group *group);

//- accelerated versions
SYMS_API SYMS_U64      syms_group_sec_number_from_voff__accelerated(SYMS_Group *group, SYMS_U64 voff);
SYMS_API SYMS_U64      syms_group_sec_number_from_foff__accelerated(SYMS_Group *group, SYMS_U64 foff);

SYMS_API SYMS_U64Maybe syms_group_sec_voff_from_foff__accelerated(SYMS_Group *group, SYMS_U64 voff);
SYMS_API SYMS_U64Maybe syms_group_sec_foff_from_voff__accelerated(SYMS_Group *group, SYMS_U64 foff);

SYMS_API SYMS_UnitID   syms_group_uid_from_voff__accelerated(SYMS_Group *group, SYMS_U64 voff);
SYMS_API SYMS_SymbolID syms_group_proc_sid_from_uid_voff__accelerated(SYMS_Group *group, SYMS_UnitID uid,
                                                                      SYMS_U64 voff);
SYMS_API SYMS_SymbolID syms_group_var_sid_from_uid_voff__accelerated(SYMS_Group *group, SYMS_UnitID uid,
                                                                     SYMS_U64 voff);

SYMS_API SYMS_Line     syms_group_line_from_uid_voff__accelerated(SYMS_Group *group, SYMS_UnitID uid,
                                                                  SYMS_U64 voff);
SYMS_API SYMS_U64RangeArray syms_group_vranges_from_uid_line__accelerated(SYMS_Group *group, SYMS_UnitID uid,
                                                                          SYMS_FileID file_id, SYMS_U32 line);

//- line info binary search helper
SYMS_API SYMS_U64      syms_index_from_n__u32__binary_search_round_up(SYMS_U32 *v, SYMS_U64 count, SYMS_U32 n);

//- line-to-addr map helpers
SYMS_API void          syms_line_to_addr_line_sort(SYMS_FileToLineToAddrLooseLine **array, SYMS_U64 count);
SYMS_API void          syms_line_to_addr_line_sort__rec(SYMS_FileToLineToAddrLooseLine **array, SYMS_U64 count);

////////////////////////////////
// NOTE(allen): Syms Group Type Graph Functions

SYMS_API SYMS_TypeNode* syms_group_type_from_usid(SYMS_Group *group, SYMS_USID usid);
SYMS_API SYMS_TypeNode* syms_group_type_from_usid__rec(SYMS_Group *group, SYMS_USID usid);

SYMS_API SYMS_TypeMemberArray* syms_group_members_from_type(SYMS_Group *group, SYMS_TypeNode *type);
SYMS_API SYMS_EnumInfoArray*   syms_group_enum_members_from_type(SYMS_Group *group, SYMS_TypeNode *type);

SYMS_API SYMS_U64       syms_group_type_size_from_usid(SYMS_Group *group, SYMS_USID usid);

SYMS_API SYMS_TypeNode* syms_group_type_basic(SYMS_Group *group, SYMS_TypeKind basic_kind,
                                              SYMS_U64 size, SYMS_String8 name);
SYMS_API SYMS_TypeNode* syms_group_type_mod_from_type(SYMS_Group *group, SYMS_TypeNode *type,
                                                      SYMS_TypeModifiers mods);
SYMS_API SYMS_TypeNode* syms_group_type_ptr_from_type(SYMS_Group *group, SYMS_TypeKind ptr_kind,
                                                      SYMS_TypeNode *type);
SYMS_API SYMS_TypeNode* syms_group_type_array_from_type(SYMS_Group *group, SYMS_TypeNode *type, SYMS_U64 count);
SYMS_API SYMS_TypeNode* syms_group_type_proc_from_type(SYMS_Group *group,
                                                       SYMS_TypeNode *ret_type, SYMS_TypeNode *this_type,
                                                       SYMS_TypeNode **param_types, SYMS_U64 param_count);
SYMS_API SYMS_TypeNode* syms_group_type_member_ptr_from_type(SYMS_Group *group, SYMS_TypeNode *container,
                                                             SYMS_TypeNode *type);

SYMS_API SYMS_String8   syms_group_string_from_type(SYMS_Arena *arena, SYMS_TypeNode *type);

SYMS_API void           syms_group_lhs_string_from_type(SYMS_Arena *arena, SYMS_TypeNode *type,
                                                        SYMS_String8List *out);
SYMS_API void           syms_group_rhs_string_from_type(SYMS_Arena *arena, SYMS_TypeNode *type,
                                                        SYMS_String8List *out);

SYMS_API void           syms_group_lhs_string_from_type_skip_return(SYMS_Arena *arena, SYMS_TypeNode *type,
                                                                    SYMS_String8List *out);

SYMS_API SYMS_U64       syms_type_usid_hash(SYMS_USID usid);
SYMS_API SYMS_TypeNode* syms_type_from_usid(SYMS_TypeUSIDBuckets *buckets, SYMS_USID usid);
SYMS_API void           syms_type_usid_buckets_insert(SYMS_Arena *arena, SYMS_TypeUSIDBuckets *buckets,
                                                      SYMS_USID key, SYMS_TypeNode *type);

SYMS_API SYMS_U64       syms_type_content_hash(SYMS_String8 data);
SYMS_API SYMS_TypeNode* syms_type_from_content(SYMS_TypeContentBuckets *buckets, SYMS_String8 data);
SYMS_API SYMS_String8   syms_type_content_buckets_insert(SYMS_Arena *arena, SYMS_TypeContentBuckets *buckets,
                                                         SYMS_String8 key, SYMS_TypeNode *type);

////////////////////////////////
//~ NOTE(allen): Syms File Map

SYMS_API SYMS_Name2FileIDMap* syms_group_file_map(SYMS_Group *group);


#endif //SYMS_GROUP_H
