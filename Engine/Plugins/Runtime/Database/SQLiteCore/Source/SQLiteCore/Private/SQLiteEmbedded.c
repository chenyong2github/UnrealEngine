// Copyright Epic Games, Inc. All Rights Reserved.

/** Needed to define USING_CODE_ANALYSIS */
#if PLATFORM_WINDOWS
#include "Windows/WindowsPlatformCodeAnalysis.h"
// This is a temporary fix to directly access SQLite API from outside this module.
// DLLEXPORT and DLLImport are the ones to remove
#define DLLEXPORT __declspec(dllexport)
#define DLLIMPORT __declspec(dllimport)
#elif defined(__clang__)
#include "Clang/ClangPlatformCodeAnalysis.h"
// This is a fix to directly access SQLite API from outside this module. 
// DLLEXPORT and DLLImport are the ones to remove
#define DLLEXPORT __attribute__((visibility("default")))
#define DLLIMPORT __attribute__((visibility("default")))
#endif

/** Include SQLite, but not if we're building for analysis as the code emits warnings */
#if !USING_CODE_ANALYSIS
#include "HAL/PreprocessorHelpers.h"
#include COMPILED_PLATFORM_HEADER(PlatformCompilerPreSetup.h)
THIRD_PARTY_INCLUDES_START
#include "sqlite/sqlite3.inl"
THIRD_PARTY_INCLUDES_END
#endif
