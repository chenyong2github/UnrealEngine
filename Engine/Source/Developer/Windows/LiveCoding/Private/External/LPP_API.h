// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#pragma once

/******************************************************************************/
/* HOOKS                                                                      */
/******************************************************************************/

// concatenates two preprocessor tokens, even when the tokens themselves are macros
#define LPP_CONCATENATE_HELPER_HELPER(_a, _b)		_a##_b
#define LPP_CONCATENATE_HELPER(_a, _b)				LPP_CONCATENATE_HELPER_HELPER(_a, _b)
#define LPP_CONCATENATE(_a, _b)						LPP_CONCATENATE_HELPER(_a, _b)

// generates a unique identifier inside a translation unit
#define LPP_IDENTIFIER(_identifier)					LPP_CONCATENATE(_identifier, __LINE__)

// custom section names for hooks
#define LPP_PREPATCH_SECTION					".lpp_prepatch_hooks"
#define LPP_POSTPATCH_SECTION					".lpp_postpatch_hooks"
#define LPP_COMPILE_START_SECTION				".lpp_compile_start_hooks"
#define LPP_COMPILE_SUCCESS_SECTION				".lpp_compile_success_hooks"
#define LPP_COMPILE_ERROR_SECTION				".lpp_compile_error_hooks"
#define LPP_COMPILE_ERROR_MESSAGE_SECTION		".lpp_compile_error_message_hooks"
// BEGIN EPIC MOD - Add the ability for pre and post compile notifications
#define LPP_PRECOMPILE_SECTION					".lpp_precompile_hooks"
#define LPP_POSTCOMPILE_SECTION					".lpp_postcompile_hooks"
// END EPIC MOD

// register a pre-patch hook in a custom section
#define LPP_PREPATCH_HOOK(_function)																							\
	__pragma(section(LPP_PREPATCH_SECTION, read))																				\
	__declspec(allocate(LPP_PREPATCH_SECTION)) extern void (*LPP_IDENTIFIER(lpp_prepatch_hook_function))(void) = &_function

// register a post-patch hook in a custom section
#define LPP_POSTPATCH_HOOK(_function)																							\
	__pragma(section(LPP_POSTPATCH_SECTION, read))																				\
	__declspec(allocate(LPP_POSTPATCH_SECTION)) extern void (*LPP_IDENTIFIER(lpp_postpatch_hook_function))(void) = &_function

// register a compile start hook in a custom section
#define LPP_COMPILE_START_HOOK(_function)																								\
	__pragma(section(LPP_COMPILE_START_SECTION, read))																					\
	__declspec(allocate(LPP_COMPILE_START_SECTION)) extern void (*LPP_IDENTIFIER(lpp_compile_start_hook_function))(void) = &_function

// register a compile success hook in a custom section
#define LPP_COMPILE_SUCCESS_HOOK(_function)																									\
	__pragma(section(LPP_COMPILE_SUCCESS_SECTION, read))																					\
	__declspec(allocate(LPP_COMPILE_SUCCESS_SECTION)) extern void (*LPP_IDENTIFIER(lpp_compile_success_hook_function))(void) = &_function

// register a compile error hook in a custom section
#define LPP_COMPILE_ERROR_HOOK(_function)																										\
	__pragma(section(LPP_COMPILE_ERROR_SECTION, read))																							\
	__declspec(allocate(LPP_COMPILE_ERROR_SECTION)) extern void (*LPP_IDENTIFIER(lpp_compile_error_hook_function))(void) = &_function

// register a compile error message hook in a custom section
#define LPP_COMPILE_ERROR_MESSAGE_HOOK(_function)																												\
	__pragma(section(LPP_COMPILE_ERROR_MESSAGE_SECTION, read))																									\
	__declspec(allocate(LPP_COMPILE_ERROR_MESSAGE_SECTION)) extern void (*LPP_IDENTIFIER(lpp_compile_error_message_hook_function))(const wchar_t*) = &_function

// BEGIN EPIC MOD - Add the ability for pre and post compile notifications
// register a pre-compile hook in a custom section
#define LPP_PRECOMPILE_HOOK(_function)																												\
	__pragma(section(LPP_PRECOMPILE_SECTION, read))																									\
	__declspec(allocate(LPP_PRECOMPILE_SECTION)) extern void (*LPP_IDENTIFIER(lpp_precompile_hook_function))(void) = &_function

// register a post-compile hook in a custom section
#define LPP_POSTCOMPILE_HOOK(_function)																												\
	__pragma(section(LPP_POSTCOMPILE_SECTION, read))																								\
	__declspec(allocate(LPP_POSTCOMPILE_SECTION)) extern void (*LPP_IDENTIFIER(lpp_postcompile_hook_function))(void) = &_function
// END EPIC MOD

/******************************************************************************/
/* API                                                                        */
/******************************************************************************/

// version string
#define LPP_VERSION "1.5.2"

#ifdef __cplusplus
#	define LPP_NS_BEGIN			namespace lpp {
#	define LPP_NS_END			}
#	define LPP_API				inline
#else
#	define LPP_NS_BEGIN
#	define LPP_NS_END
#	define LPP_API				static inline
#endif

// helper macros to call a function in a DLL with an arbitrary signature without a compiler warning
#define LPP_CALL1(_module, _functionName, _return, _args1)								((_return (__cdecl*)(_args1))((uintptr_t)GetProcAddress(_module, _functionName)))
#define LPP_CALL2(_module, _functionName, _return, _args1, _args2)						((_return (__cdecl*)(_args1, _args2))((uintptr_t)GetProcAddress(_module, _functionName)))
#define LPP_CALL3(_module, _functionName, _return, _args1, _args2, _args3)				((_return (__cdecl*)(_args1, _args2, _args3))((uintptr_t)GetProcAddress(_module, _functionName)))
#define LPP_CALL4(_module, _functionName, _return, _args1, _args2, _args3, _args4)		((_return (__cdecl*)(_args1, _args2, _args3, _args4))((uintptr_t)GetProcAddress(_module, _functionName)))


LPP_NS_BEGIN

enum RestartBehaviour
{
	// BEGIN EPIC MODS - Use UE4 codepath for termination to ensure logs are flushed and session analytics are sent
	LPP_RESTART_BEHAVIOR_REQUEST_EXIT,				// FPlatforMisc::RequestExit(true)
	// END EPIC MODS
	LPP_RESTART_BEHAVIOUR_DEFAULT_EXIT,				// ExitProcess()
	LPP_RESTART_BEHAVIOUR_EXIT_WITH_FLUSH,			// exit()
	LPP_RESTART_BEHAVIOUR_EXIT,						// _Exit()
	LPP_RESTART_BEHAVIOUR_INSTANT_TERMINATION		// TerminateProcess
};

LPP_NS_END


#undef LPP_CALL1
#undef LPP_CALL2
#undef LPP_CALL3
