// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreTypes.h"
#include "Trace/Trace.h"
#include "Trace/Detail/LogScope.h"

////////////////////////////////////////////////////////////////////////////////
// Fwd declare ELLMTag
enum class ELLMTag : uint8;
// Fwd declare LLM private tag data
namespace UE {
	namespace LLMPrivate {
		class FTagData;
	}
}

////////////////////////////////////////////////////////////////////////////////
CORE_API int32	MemoryTrace_AnnounceCustomTag(int32 Tag, int32 ParentTag, const TCHAR* Display);
CORE_API int32	MemoryTrace_AnnounceFNameTag(const class FName& TagName);
CORE_API int32	MemoryTrace_GetActiveTag();

////////////////////////////////////////////////////////////////////////////////

#if !defined(USE_MEMORY_TRACE_TAGS) && UE_TRACE_ENABLED && UE_BUILD_DEVELOPMENT
	#if PLATFORM_WINDOWS
		#define USE_MEMORY_TRACE_TAGS 1
	#endif
#endif

#if !defined(USE_MEMORY_TRACE_TAGS)
	#define USE_MEMORY_TRACE_TAGS 0
#endif

#if USE_MEMORY_TRACE_TAGS

////////////////////////////////////////////////////////////////////////////////

/**
  * Used to associate any allocation within this scope to a given tag.
  *
  * We need to be able to convert the three types of inputs to LLM scopes:
  * - ELLMTag, an uint8 with fixed categories. There are three sub ranges
	  Generic tags, platform and project tags.
  * - FName, free form string, for example a specific asset.
  * - TagData, an opaque pointer from LLM.
  *
  */
class FMemScope
{
public:
	CORE_API FMemScope(int32 InTag);
	CORE_API FMemScope(ELLMTag InTag);
	CORE_API FMemScope(const class FName& InName);
	CORE_API FMemScope(const UE::LLMPrivate::FTagData* TagData);
	CORE_API ~FMemScope();
private:
	void ActivateScope(int32 InTag);
	UE::Trace::Private::FScopedLogScope Inner;
	int32 PrevTag;
};

/**
 * Used order to keep the tag for memory that is being reallocated.
 */
class FMemScopeRealloc
{
public:
	CORE_API FMemScopeRealloc(uint64 InPtr);
	CORE_API ~FMemScopeRealloc();
private:
	UE::Trace::Private::FScopedLogScope Inner;
};

////////////////////////////////////////////////////////////////////////////////

#define UE_MEMSCOPE(InTag, InTracker)				FMemScope PREPROCESSOR_JOIN(MemScope,__LINE__)(InTag);
#define UE_MEMSCOPE_REALLOC(InPtr, InTracker)		FMemScopeRealloc PREPROCESSOR_JOIN(MemReallocScope,__LINE__)((uint64)InPtr);

#else // USE_MEMORY_TRACE_TAGS

////////////////////////////////////////////////////////////////////////////////
#define UE_MEMSCOPE(...)
#define UE_MEMSCOPE_REALLOC(...)

#endif // USE_MEMORY_TRACE_TAGS

