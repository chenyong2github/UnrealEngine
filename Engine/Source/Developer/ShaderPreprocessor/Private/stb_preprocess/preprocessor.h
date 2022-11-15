// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include <stdlib.h>

enum
{
	PP_RESULT_ok,
	PP_RESULT_supplementary,			 // extra information associated with previous warning/error
	PP_RESULT_undef_of_undefined_macro,	 // by default this is not an error

	PP_RESULT_internal_error_out_of_memory,

	PP_RESULT_ERROR = 4,

	PP_RESULT_counter_overflowed,
	PP_RESULT_too_many_arguments_to_macro,
	PP_RESULT_invalid_char,
	PP_RESULT_unrecognized_directive,
	PP_RESULT_directive_not_at_start_of_line,
	PP_RESULT_unexpected_character_in_directive,
	PP_RESULT_unexpected_character_after_end_of_directive,
	PP_RESULT_undef_of_predefined_macro,
	PP_RESULT_identifier_too_long,

	PP_RESULT_count
};

enum
{
	PP_RESULT_MODE_error,  // default error type is 0 to simplify initialization
	PP_RESULT_MODE_warning,
	PP_RESULT_MODE_supplementary,

	PP_RESULT_MODE_warning_fast,
	PP_RESULT_MODE_no_warning
};

struct macro_definition;
struct stb_arena;

typedef struct
{
	char* filename;
	int line_number;
	int column;
} pp_where;

typedef struct
{
	char* message;
	int diagnostic_code;
	int error_level;
	pp_where* where;
} pp_diagnostic;

extern void preprocessor_test(void);
extern void init_preprocessor(void);
extern char* preprocess_file(char* output_storage,
							 char* filename,
							 char** include_paths,
							 int num_include_paths,
							 char** sys_include_paths,
							 int num_sys_include_paths,
							 struct macro_definition** predefined_macros,
							 int num_predefined_macros,
							 pp_diagnostic** pd,
							 int* num_pd);
extern void preprocessor_file_free(char* text, pp_diagnostic* pd);
extern struct macro_definition* pp_define(struct stb_arena* a, char* p);
extern unsigned int preprocessor_hash(char* data, size_t len);	// hash function for string of length 'len', roughly xxhash32
