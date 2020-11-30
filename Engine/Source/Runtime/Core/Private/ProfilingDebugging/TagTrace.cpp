// Copyright Epic Games, Inc. All Rights Reserved.
#include "ProfilingDebugging/TagTrace.h"

#include "CoreTypes.h"
#include "MemoryTrace.h"
#include "HAL/LowLevelMemTracker.h"
#include "Trace/Trace.inl"

#if USE_MEMORY_TRACE_TAGS

////////////////////////////////////////////////////////////////////////////////

UE_TRACE_CHANNEL_EXTERN(MemAllocChannel);

UE_TRACE_EVENT_BEGIN(Memory, TagSpec)
	UE_TRACE_EVENT_FIELD(int32, Tag)
	UE_TRACE_EVENT_FIELD(int32, Parent)
	UE_TRACE_EVENT_FIELD(UE::Trace::AnsiString, Display)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Memory, MemoryScope)
	UE_TRACE_EVENT_FIELD(int32, Tag)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Memory, MemoryScopeRealloc)
	UE_TRACE_EVENT_FIELD(uint64, Ptr)
UE_TRACE_EVENT_END()



////////////////////////////////////////////////////////////////////////////////
// Per thread active tag, i.e. the top level FMemScope
thread_local int32 GActiveTag;

////////////////////////////////////////////////////////////////////////////////
FMemScope::FMemScope(int32 InTag)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(MemAllocChannel))
	{
		ActivateScope(InTag);
	}
}

////////////////////////////////////////////////////////////////////////////////
FMemScope::FMemScope(ELLMTag InTag)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(MemAllocChannel))
	{
		ActivateScope(static_cast<int32>(InTag));
	}
}

////////////////////////////////////////////////////////////////////////////////
FMemScope::FMemScope(FName InName)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(MemAllocChannel))
	{
		ActivateScope(MemoryTrace_AnnounceFNameTag(InName));
	}
}

////////////////////////////////////////////////////////////////////////////////
FMemScope::FMemScope(const UE::LLMPrivate::FTagData* TagData)
{
	// TagData is opaque so we cant really use the input, additionally we 
	// cannot count on LLM being active. Instead we have inserted an explicit 
	// Trace scope directly following the LLM scope. 
}

////////////////////////////////////////////////////////////////////////////////
void FMemScope::ActivateScope(int32 InTag)
{
	if (auto LogScope = FMemoryMemoryScopeFields::LogScopeType::ScopedEnter<FMemoryMemoryScopeFields>())
	{
		if (const auto& __restrict MemoryScope = *(FMemoryMemoryScopeFields*)(&LogScope))
		{
			Inner.SetActive();
			LogScope += LogScope << MemoryScope.Tag(InTag);
			PrevTag = GActiveTag;
			GActiveTag = InTag;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
FMemScope::~FMemScope()
{
	if (Inner.bActive)
	{
		GActiveTag = PrevTag;
	}
}

////////////////////////////////////////////////////////////////////////////////
FMemScopeRealloc::FMemScopeRealloc(uint64 InPtr)
{
	if (InPtr != 0 && TRACE_PRIVATE_CHANNELEXPR_IS_ENABLED(MemAllocChannel))
	{
		if (auto LogScope = FMemoryMemoryScopeReallocFields::LogScopeType::ScopedEnter<FMemoryMemoryScopeReallocFields>())
		{
			if (const auto& __restrict MemoryScope = *(FMemoryMemoryScopeReallocFields*)(&LogScope))
			{
				Inner.SetActive(), LogScope += LogScope << MemoryScope.Ptr(InPtr);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
FMemScopeRealloc::~FMemScopeRealloc()
{
}



/////////////////////////////////////////////////////////////////////////////////

/**
 * Utility class that manages tracing the specification of unique LLM tags 
 * and custom name based tags.
 */
class FTagTrace
{
public:
					FTagTrace(FMalloc* InMalloc);
	void			AnnounceGenericTags();
	void 			AnnounceTagDeclarations();
	static void 	OnAnnounceTagDeclaration(FLLMTagDeclaration& TagDeclaration);
	int32			AnnounceCustomTag(int32 Tag, int32 ParentTag, const ANSICHAR* Display);
	int32 			AnnounceFNameTag(FName TagName);

private:
	/**
	 * Allocator used to allocate from the untracked FMalloc instance.
	 */
	class FTagAllocator : public TSizedHeapAllocator<32>
	{
	public:

		class ForAnyElementType : public TSizedHeapAllocator<32>::ForAnyElementType
		{
		public:
			ForAnyElementType()
				: Data(nullptr)
			{}

			FORCEINLINE FScriptContainerElement* GetAllocation() const { return Data; }
			FORCEINLINE bool HasAllocation() const { return !!Data; }
			void ResizeAllocation(SizeType PreviousNumElements, SizeType NumElements, SIZE_T NumBytesPerElement)
			{
				check(FTagTrace::Malloc);
				if (Data || NumElements)
				{
					Data = (FScriptContainerElement*)FTagTrace::Malloc->Realloc(Data, NumElements * NumBytesPerElement);
				}
			}

		private:
			FScriptContainerElement* Data;
		};

		template<typename ElementType>
		class ForElementType : public ForAnyElementType
		{
		public:
			FORCEINLINE ElementType* GetAllocation() const
			{
				return (ElementType*)ForAnyElementType::GetAllocation();
			}
		};

	};
	typedef TSparseArrayAllocator<FTagAllocator> FTagSparseArrayAllocator;
	typedef TInlineAllocator<1,FTagAllocator> FTagHashAllocator; 
	typedef TSetAllocator<FTagSparseArrayAllocator, FTagHashAllocator> FTagSetAllocator;
	typedef TSet<int32, DefaultKeyFuncs<int32>, FTagSetAllocator> FTagSet;

	FCriticalSection		Cs;
	FTagSet 				AnnouncedNames;
	static FMalloc* 		Malloc;
};

FMalloc* FTagTrace::Malloc = nullptr;
static FTagTrace* GTagTrace = nullptr;

////////////////////////////////////////////////////////////////////////////////
FTagTrace::FTagTrace(FMalloc* InMalloc) 
{
	Malloc = InMalloc;
	AnnouncedNames.Reserve(1024);
	AnnounceGenericTags();
	AnnounceTagDeclarations();
}

////////////////////////////////////////////////////////////////////////////////
void FTagTrace::AnnounceGenericTags()
{
	#define TRACE_TAG_SPEC(Enum,Str,Stat,Group,ParentTag)\
	{\
		const uint32 DisplayLen = FCStringAnsi::Strlen(Str);\
		UE_TRACE_LOG(Memory, TagSpec, MemAllocChannel, DisplayLen * sizeof(ANSICHAR))\
			<< TagSpec.Tag((int32) ELLMTag::Enum)\
			<< TagSpec.Parent((int32) ParentTag)\
			<< TagSpec.Display(Str, DisplayLen);\
	}
	LLM_ENUM_GENERIC_TAGS(TRACE_TAG_SPEC);
	#undef TRACE_TAG_SPEC
}

////////////////////////////////////////////////////////////////////////////////
void FTagTrace::AnnounceTagDeclarations()
{
	FLLMTagDeclaration*& List = FLLMTagDeclaration::GetList();
	while (List)
	{
		OnAnnounceTagDeclaration(*List);
		List = List->Next;
	}
	FLLMTagDeclaration::SetCreationCallback(FTagTrace::OnAnnounceTagDeclaration);
}

////////////////////////////////////////////////////////////////////////////////
void FTagTrace::OnAnnounceTagDeclaration(FLLMTagDeclaration& TagDeclaration)
{
	TagDeclaration.ConstructUniqueName();
	GTagTrace->AnnounceFNameTag(TagDeclaration.GetUniqueName());
}

////////////////////////////////////////////////////////////////////////////////
int32 FTagTrace::AnnounceFNameTag(FName Name)
{
	FScopeLock _(&Cs);
	const int32 NameIndex = Name.GetDisplayIndex().ToUnstableInt();
	if (AnnouncedNames.Contains(NameIndex))
	{
		return NameIndex;
	}

	AnnouncedNames.Emplace(NameIndex);
	ANSICHAR NameString[NAME_SIZE];
	Name.GetPlainANSIString(NameString);
	return AnnounceCustomTag(NameIndex, -1, NameString);
}

////////////////////////////////////////////////////////////////////////////////
int32 FTagTrace::AnnounceCustomTag(int32 Tag, int32 ParentTag, const ANSICHAR* Display)
{		
	const uint32 DisplayLen = FCStringAnsi::Strlen(Display);
	UE_TRACE_LOG(Memory, TagSpec, MemAllocChannel, DisplayLen * sizeof(ANSICHAR))
		<< TagSpec.Tag(Tag)
		<< TagSpec.Parent(ParentTag)
		<< TagSpec.Display(Display, DisplayLen);
	return Tag;
}

#endif //USE_MEMORY_TRACE_TAGS


////////////////////////////////////////////////////////////////////////////////
void MemoryTrace_InitTags(FMalloc* InMalloc)
{
#if USE_MEMORY_TRACE_TAGS
	GTagTrace = (FTagTrace*)InMalloc->Malloc(sizeof(FTagTrace), alignof(FTagTrace));
	new (GTagTrace) FTagTrace(InMalloc);
#endif
}

////////////////////////////////////////////////////////////////////////////////
int32 MemoryTrace_AnnounceCustomTag(int32 Tag, int32 ParentTag, const TCHAR* Display)
{
#if USE_MEMORY_TRACE_TAGS
	//todo: How do we check if tag trace is active?
	if (GTagTrace)
	{
		return GTagTrace->AnnounceCustomTag(Tag, ParentTag, TCHAR_TO_ANSI(Display));
	}
#endif
	return -1;
}

////////////////////////////////////////////////////////////////////////////////
int32 MemoryTrace_AnnounceFNameTag(FName TagName)
{
#if USE_MEMORY_TRACE_TAGS
	if (GTagTrace)
	{
		return GTagTrace->AnnounceFNameTag(TagName);
	}
#endif
	return -1;
}

////////////////////////////////////////////////////////////////////////////////
int32 MemoryTrace_GetActiveTag()
{
#if USE_MEMORY_TRACE_TAGS
	return GActiveTag;
#else
	return -1;
#endif
}

