// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/LowLevelMemTracker.h"
#include "ProfilingDebugging/MetadataTrace.h"
#include "ProfilingDebugging/TagTrace.h"

namespace UE
{
	// Restores an inherited contex for the current scope.
	// An instance must be obtained by calling `FInheritedContextBase::RestoreInheritedContext()`
	class FInheritedContextScope
	{
	private:
		UE_NONCOPYABLE(FInheritedContextScope);

		friend class FInheritedContextBase; // allow construction only by `FInheritedContextBase`

		FInheritedContextScope(
#if ENABLE_LOW_LEVEL_MEM_TRACKER
			const UE::LLMPrivate::FTagData* InInheritedLLMTag
	#if UE_MEMORY_TAGS_TRACE_ENABLED
			,
	#endif
#endif
#if UE_MEMORY_TAGS_TRACE_ENABLED
			int32 InInheritedMemTag
#endif
		)
#if ENABLE_LOW_LEVEL_MEM_TRACKER
			: LLMScope(InInheritedLLMTag, false /* bIsStatTag */, ELLMTagSet::None, ELLMTracker::Default)
	#if UE_MEMORY_TAGS_TRACE_ENABLED
			,
	#endif
#endif
#if UE_MEMORY_TAGS_TRACE_ENABLED
	#if !ENABLE_LOW_LEVEL_MEM_TRACKER
			:
	#endif
			MemScope(InInheritedMemTag)
#endif
		{
		}

	private:
#if ENABLE_LOW_LEVEL_MEM_TRACKER
		FLLMScope LLMScope;
#endif

#if UE_MEMORY_TAGS_TRACE_ENABLED
		FMemScope MemScope;
#endif
	};

	// this class extends the inherited context (see private members for what the inherited context is) to cover async execution. 
	// Is intended to be used as a base class, if the inherited context is compiled out it takes 0 space
	class FInheritedContextBase
	{
	public:
		// must be called in the inherited context, e.g. on launching an async task
		void CaptureInheritedContext()
		{
#if ENABLE_LOW_LEVEL_MEM_TRACKER
			InheritedLLMTag = FLowLevelMemTracker::bIsDisabled ? nullptr : FLowLevelMemTracker::Get().GetActiveTagData(ELLMTracker::Default);
#endif

#if UE_MEMORY_TAGS_TRACE_ENABLED
			InheritedMemTag = MemoryTrace_GetActiveTag();
#endif

#if UE_TRACE_METADATA_ENABLED
			InheritedMetadataId = UE_TRACE_METADATA_SAVE_STACK();
#endif
		}

		// must be called where the inherited context should be restored, e.g. at the start of an async task execution
		UE_NODISCARD CORE_API FInheritedContextScope RestoreInheritedContext();

	private:
#if ENABLE_LOW_LEVEL_MEM_TRACKER
		const UE::LLMPrivate::FTagData* InheritedLLMTag;
#endif

#if UE_MEMORY_TAGS_TRACE_ENABLED
		int32 InheritedMemTag;
#endif

#if UE_TRACE_METADATA_ENABLED
		uint32 InheritedMetadataId;
#endif
	};
}