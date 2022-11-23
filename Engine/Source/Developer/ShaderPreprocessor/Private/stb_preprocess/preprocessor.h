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


#ifdef __cplusplus
#define STB_PP_DEF extern "C"
#else
#define STB_PP_DEF extern
#endif

// callback function can be optionally specified to init_preprocessor to override file loading functionality
// it's probably necessary to provide the freefile callback as well if you provide this (unless you are ok with
// the default behaviour of calling "free" on the returned pointer)
typedef const char* (*loadfile_callback_func)(const char* filename, void* custom_context, size_t* out_length);

// callback function can be optionally specified to init_preprocessor to override freeing loaded files
// it's probably necessary to provide the loadfile callback as well if you provide this
typedef void (*freefile_callback_func)(const char* filename, const char* loaded_file, void* custom_context);

// callback function can be optionally specified to init_preprocessor to override include resolution; in the
// current incarnation this will still attempt file-based loading using the default behaviour in the event
// that this callback returns null for a given file path.
// note: returned resolved paths are expected to be allocated for the lifetime of preprocessor execution; 
// for simplicity use stb_arena_alloc_string using the given "path_arena" stb_arena
typedef const char* (*resolveinclude_callback_func)(const char* path, unsigned int path_len, const char* parent, struct stb_arena* path_arena, void* custom_context);

STB_PP_DEF void preprocessor_test(void);
STB_PP_DEF void init_preprocessor(
	loadfile_callback_func load_callback,
	freefile_callback_func free_callback,
	resolveinclude_callback_func resolve_callback);
STB_PP_DEF char* preprocess_file(
	char* output_storage,
	const char* filename,
	void* custom_context,
	char** include_paths,
	int num_include_paths,
	char** sys_include_paths,
	int num_sys_include_paths,
	struct macro_definition** predefined_macros,
	int num_predefined_macros,
	pp_diagnostic** pd,
	int* num_pd);
STB_PP_DEF void preprocessor_file_free(char* text, pp_diagnostic* pd);
STB_PP_DEF struct macro_definition* pp_define(struct stb_arena* a, const char* p);
STB_PP_DEF unsigned int preprocessor_hash(const char* data, size_t len);	// hash function for string of length 'len', roughly xxhash32
