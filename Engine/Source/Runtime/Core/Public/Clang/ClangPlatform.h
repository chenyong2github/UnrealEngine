// Copyright Epic Games, Inc. All Rights Reserved.

/*================================================================================
	ClangPlatform.h: Setup for any Clang-using platform
==================================================================================*/

#pragma once

#if defined(__cpp_if_constexpr)
	#define PLATFORM_COMPILER_HAS_IF_CONSTEXPR 1
#else
	#define PLATFORM_COMPILER_HAS_IF_CONSTEXPR 0
#endif

#if defined(__cpp_fold_expressions)
	#define PLATFORM_COMPILER_HAS_FOLD_EXPRESSIONS 1
#else
	#define PLATFORM_COMPILER_HAS_FOLD_EXPRESSIONS 0
#endif

#define PLATFORM_RETURN_ADDRESS()			__builtin_return_address(0)
#define PLATFORM_RETURN_ADDRESS_POINTER()	__builtin_frame_address(0)
