// Copyright Epic Games, Inc. All Rights Reserved.
/* date = September 16th 2021 1:52 pm */

#ifndef SYMS_DATA_STRUCTURES_H
#define SYMS_DATA_STRUCTURES_H

////////////////////////////////
//~ NOTE(allen): String Cons Structure

// deduplicates equivalent strings

typedef struct SYMS_StringConsNode{
  struct SYMS_StringConsNode *next;
  SYMS_String8 string;
  SYMS_U64 hash;
} SYMS_StringConsNode;

typedef struct SYMS_StringCons{
  SYMS_StringConsNode **buckets;
  SYMS_U64 bucket_count;
} SYMS_StringCons;

////////////////////////////////
//~ NOTE(allen): 1D Spatial Mapping Structure

// assigns a value to ranges of unsigned 64-bit values
// ranges specified in half-open intervals: [min,max)
// ranges must be non-overlapping

typedef struct SYMS_SpatialMap1DRange{
  SYMS_U64Range range;
  SYMS_U64 val;
} SYMS_SpatialMap1DRange;

typedef struct SYMS_SpatialMap1D{
  SYMS_SpatialMap1DRange *ranges;
  SYMS_U64 count;
} SYMS_SpatialMap1D;

//- loose version
typedef struct SYMS_SpatialMap1DNode{
  struct SYMS_SpatialMap1DNode *next;
  SYMS_U64Range range;
  SYMS_U64RangeArray ranges;
  SYMS_U64 val;
} SYMS_SpatialMap1DNode;

typedef struct SYMS_SpatialMap1DLoose{
  SYMS_SpatialMap1DNode *first;
  SYMS_SpatialMap1DNode *last;
  SYMS_U64 total_count;
} SYMS_SpatialMap1DLoose;

////////////////////////////////
//~ NOTE(allen): File Mapping Structure ({UnitID,FileID} -> String)

// maps a unit-id,file-id pair to a string
// organized as a hash table to opimize for key based lookups

typedef struct SYMS_FileID2NameNode{
  struct SYMS_FileID2NameNode *next;
  // key
  SYMS_UnitID uid;
  SYMS_FileID file_id;
  // value
  SYMS_String8 name;
} SYMS_FileID2NameNode;

typedef struct SYMS_FileID2NameMap{
  SYMS_FileID2NameNode **buckets;
  SYMS_U64 bucket_count;
  SYMS_U64 count;
} SYMS_FileID2NameMap;

////////////////////////////////
//~ NOTE(allen): File Mapping Structure (String -> {UnitID,FileID})

// maps strings to a set of unit-id,file-id pairs
// oragnized in an array of strings with each string
//  equipped with an array of unit-id,file-id pairs
//
// we don't organize this as a hash table because having a
//  list of all known source files is useful and there are
//  many string matching rules that might want to be used
//  for lookups into this data

typedef struct SYMS_Name2FileIDMapUnit{
  SYMS_UnitID uid;
  SYMS_FileID file_id;
} SYMS_Name2FileIDMapUnit;

typedef struct SYMS_Name2FileIDMapFile{
  SYMS_String8 name;
  SYMS_Name2FileIDMapUnit *units;
  SYMS_U64 unit_count;
} SYMS_Name2FileIDMapFile;

typedef struct SYMS_Name2FileIDMap{
  SYMS_Name2FileIDMapFile *files;
  SYMS_U64 file_count;
} SYMS_Name2FileIDMap;

//- loose version
typedef struct SYMS_Name2FileIDMapUnitNode{
  struct SYMS_Name2FileIDMapUnitNode *next;
  SYMS_UnitID uid;
  SYMS_FileID file_id;
} SYMS_Name2FileIDMapUnitNode;

typedef struct SYMS_Name2FileIDMapFileNode{
  struct SYMS_Name2FileIDMapFileNode *next;
  SYMS_String8 name;
  SYMS_Name2FileIDMapUnitNode *first;
  SYMS_Name2FileIDMapUnitNode *last;
  SYMS_U64 count;
} SYMS_Name2FileIDMapFileNode;

typedef struct SYMS_Name2FileIDMapLoose{
  SYMS_Name2FileIDMapFileNode *first;
  SYMS_Name2FileIDMapFileNode *last;
  SYMS_U64 count;
} SYMS_Name2FileIDMapLoose;


////////////////////////////////
//~ NOTE(allen): ID Mapping Structure

// maps unsigned 64-bit values to arbitrary user pointers
// organized as a hash table to opimize key based lookups

#define SYMS_ID_MAP_NODE_CAP 3

typedef struct SYMS_IDMapNode{
  struct SYMS_IDMapNode *next;
  SYMS_U64 count;
  SYMS_U64 key[SYMS_ID_MAP_NODE_CAP];
  void *val[SYMS_ID_MAP_NODE_CAP];
} SYMS_IDMapNode;

typedef struct SYMS_IDMap{
  SYMS_IDMapNode **buckets;
  SYMS_U64 bucket_count;
  SYMS_U64 node_count;
} SYMS_IDMap;


////////////////////////////////
//~ NOTE(allen): String Cons Functions

SYMS_API SYMS_StringCons syms_string_cons_alloc(SYMS_Arena *arena, SYMS_U64 bucket_count);
SYMS_API SYMS_String8    syms_string_cons(SYMS_Arena *arena, SYMS_StringCons *cons, SYMS_String8 string);


////////////////////////////////
//~ NOTE(allen): 1D Spatial Mapping Functions

//- lookups into spatial maps
SYMS_API SYMS_U64          syms_spatial_map_1d_binary_search(SYMS_SpatialMap1D *map, SYMS_U64 x);
SYMS_API SYMS_U64          syms_spatial_map_1d_index_from_point(SYMS_SpatialMap1D *map, SYMS_U64 x);
SYMS_API SYMS_U64          syms_spatial_map_1d_value_from_point(SYMS_SpatialMap1D *map, SYMS_U64 x);

//- copying spatial maps
SYMS_API SYMS_SpatialMap1D syms_spatial_map_1d_copy(SYMS_Arena *arena, SYMS_SpatialMap1D *map);

//- constructing spatial maps
SYMS_API void              syms_spatial_map_1d_loose_push(SYMS_Arena *arena, SYMS_SpatialMap1DLoose *loose,
                                                          SYMS_U64 val, SYMS_U64RangeArray ranges);
SYMS_API void              syms_spatial_map_1d_loose_push_single(SYMS_Arena *arena, SYMS_SpatialMap1DLoose *loose,
                                                                 SYMS_U64 val, SYMS_U64Range range);
SYMS_API SYMS_SpatialMap1D syms_spatial_map_1d_bake(SYMS_Arena *arena, SYMS_SpatialMap1DLoose *loose);

SYMS_API SYMS_B32          syms_spatial_map_1d_array_check_sorted(SYMS_SpatialMap1DRange *ranges, SYMS_U64 count);
SYMS_API void              syms_spatial_map_1d_array_sort(SYMS_SpatialMap1DRange *ranges, SYMS_U64 count);
SYMS_API void              syms_spatial_map_1d_array_sort__rec(SYMS_SpatialMap1DRange *ranges, SYMS_U64 count);

//- invariants for spatial maps
SYMS_API SYMS_B32          syms_spatial_map_1d_invariants(SYMS_SpatialMap1D *map);


////////////////////////////////
//~ NOTE(allen): File Mapping Functions ({UnitID,FileID} -> String)

//- shared file id bucket definitions
SYMS_API SYMS_U64            syms_file_id_2_name_map_hash(SYMS_UnitID uid, SYMS_FileID file_id);

//- lookups into file id buckets
SYMS_API SYMS_String8        syms_file_id_2_name_map_name_from_id(SYMS_FileID2NameMap *buckets,
                                                                  SYMS_UnitID uid, SYMS_FileID file_id);

//- copying file id buckets
SYMS_API SYMS_FileID2NameMap syms_file_id_2_name_map_copy(SYMS_Arena *arena, SYMS_StringCons *cons_optional,
                                                          SYMS_FileID2NameMap *map);

//- constructing file id buckets
SYMS_API SYMS_FileID2NameMap syms_file_id_2_name_map_alloc(SYMS_Arena *arena, SYMS_U64 bucket_count);
SYMS_API void                syms_file_id_2_name_map_insert(SYMS_Arena *arena, SYMS_FileID2NameMap *map,
                                                            SYMS_UnitID uid, SYMS_FileID file_id,
                                                            SYMS_String8 name);


////////////////////////////////
//~ NOTE(allen): File Mapping Functions (String -> {UnitID,FileID})

//- copying file maps
SYMS_API SYMS_Name2FileIDMap syms_name_2_file_id_map_copy(SYMS_Arena *arena, SYMS_StringCons *cons_optional,
                                                          SYMS_Name2FileIDMap *file_map);

//- constructing file maps
SYMS_API SYMS_Name2FileIDMap syms_name_2_file_id_map_bake(SYMS_Arena *arena, SYMS_Name2FileIDMapLoose *loose);
// NOTE(allen): Strings passed to this function should all be cons'ed in the same cons structure first.
SYMS_API void syms_name_2_file_id_map_loose_push(SYMS_Arena *arena, SYMS_Name2FileIDMapLoose *map,
                                                 SYMS_String8 name_cons,
                                                 SYMS_UnitID uid, SYMS_FileID file_id);


////////////////////////////////
//~ NOTE(allen): ID Mapping Functions

//- copying id maps
SYMS_API SYMS_IDMap   syms_id_map_copy(SYMS_Arena *arena, SYMS_IDMap *map);

//- lookups into id map
SYMS_API void*        syms_id_map_ptr_from_u64(SYMS_IDMap *map, SYMS_U64 key);

//- constructing id maps
SYMS_API SYMS_IDMap   syms_id_map_alloc(SYMS_Arena *arena, SYMS_U64 bucket_count);
SYMS_API void         syms_id_map_insert(SYMS_Arena *arena, SYMS_IDMap *map, SYMS_U64 key, void *val);


////////////////////////////////
//~ NOTE(allen): Line Tables

//- lookups into line tables
SYMS_API SYMS_U64  syms_line_index_from_voff__binary_search(SYMS_Line *lines, SYMS_U64 ender_index, SYMS_U64 voff);
SYMS_API SYMS_Line syms_line_from_sequence_voff(SYMS_LineTable *line_table, SYMS_U64 seq_index, SYMS_U64 voff);

//- copying and rewriting line tables
SYMS_API SYMS_LineTable    syms_line_table_copy(SYMS_Arena *arena, SYMS_LineTable *line_table);
SYMS_API void              syms_line_table_rewrite_file_ids_in_place(SYMS_FileIDArray *file_ids,
                                                                     SYMS_LineTable *line_table_in_out);
SYMS_API SYMS_LineTable    syms_line_table_with_indexes_from_parse(SYMS_Arena *arena, SYMS_LineParseOut *parse);



////////////////////////////////
//~ NOTE(allen): Copies & Operators for Other Data Structures

SYMS_API SYMS_String8Array syms_string_array_copy(SYMS_Arena *arena, SYMS_StringCons *cons_optional,
                                                  SYMS_String8Array *array);

SYMS_API SYMS_StrippedInfoArray syms_stripped_info_copy(SYMS_Arena *arena, SYMS_StrippedInfoArray *stripped);


#endif //SYMS_DATA_STRUCTURES_H
