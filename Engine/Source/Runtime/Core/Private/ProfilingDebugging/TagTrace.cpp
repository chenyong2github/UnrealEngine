// Copyright Epic Games, Inc. All Rights Reserved.
#include "ProfilingDebugging/TagTrace.h"

#include "Experimental/Containers/GrowOnlyLockFreeHash.h"
#include "Containers/Set.h"
#include "CoreTypes.h"
#include "MemoryTrace.h"
#include "Misc/ScopeLock.h"
#include "HAL/LowLevelMemTracker.h"
#include "Trace/Trace.inl"
#include "UObject/NameTypes.h"

#if USE_MEMORY_TRACE_TAGS

////////////////////////////////////////////////////////////////////////////////

UE_TRACE_CHANNEL_EXTERN(MemAllocChannel);

UE_TRACE_EVENT_BEGIN(Memory, TagSpec, Important|NoSync)
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
FMemScope::FMemScope(const FName& InName)
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
#if ENABLE_LOW_LEVEL_MEM_TRACKER
	static void 	OnAnnounceTagDeclaration(FLLMTagDeclaration& TagDeclaration);
#endif
	int32			AnnounceCustomTag(int32 Tag, int32 ParentTag, const ANSICHAR* Display);
	int32 			AnnounceFNameTag(const FName& TagName);

private:

	struct FTagNameSetEntry
	{
		std::atomic_int32_t Data;

		int32 GetKey() const { return Data.load(std::memory_order_relaxed); }
		bool GetValue() const { return true; }
		bool IsEmpty() const { return Data.load(std::memory_order_relaxed) == 0; }			// NAME_None is treated as empty
		void SetKeyValue(int32 Key, bool Value) { Data.store(Key, std::memory_order_relaxed); }
		static uint32 KeyHash(int32 Key) { return static_cast<uint32>(Key); }
		static void ClearEntries(FTagNameSetEntry* Entries, int32 EntryCount)
		{
			memset(Entries, 0, EntryCount * sizeof(FTagNameSetEntry));
		}
	};
	typedef TGrowOnlyLockFreeHash<FTagNameSetEntry, int32, bool> FTagNameSet;

	FTagNameSet				AnnouncedNames;
	static FMalloc* 		Malloc;
};

FMalloc* FTagTrace::Malloc = nullptr;
static FTagTrace* GTagTrace = nullptr;

////////////////////////////////////////////////////////////////////////////////
FTagTrace::FTagTrace(FMalloc* InMalloc)
	: AnnouncedNames(InMalloc)
{
	Malloc = InMalloc;
	AnnouncedNames.Reserve(1024);
	AnnounceGenericTags();
	AnnounceTagDeclarations();
}

////////////////////////////////////////////////////////////////////////////////
void FTagTrace::AnnounceGenericTags()
{
#if ENABLE_LOW_LEVEL_MEM_TRACKER
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
#endif
}

////////////////////////////////////////////////////////////////////////////////
void FTagTrace::AnnounceTagDeclarations()
{
#if ENABLE_LOW_LEVEL_MEM_TRACKER
	FLLMTagDeclaration* List = FLLMTagDeclaration::GetList();
	while (List)
	{
		OnAnnounceTagDeclaration(*List);
		List = List->Next;
	}
	FLLMTagDeclaration::AddCreationCallback(FTagTrace::OnAnnounceTagDeclaration);
#endif
}

////////////////////////////////////////////////////////////////////////////////
#if ENABLE_LOW_LEVEL_MEM_TRACKER
void FTagTrace::OnAnnounceTagDeclaration(FLLMTagDeclaration& TagDeclaration)
{
	TagDeclaration.ConstructUniqueName();
	GTagTrace->AnnounceFNameTag(TagDeclaration.GetUniqueName());
}
#endif

////////////////////////////////////////////////////////////////////////////////
int32 FTagTrace::AnnounceFNameTag(const FName& Name)
{
	const int32 NameIndex = Name.GetDisplayIndex().ToUnstableInt();

	// Don't announce NAME_None, if we happen to get that passed in.  The "AnnouncedNames" container
	// is not allowed to hold "NAME_None", as zero represents an invalid key.
	if (!NameIndex)
	{
		return NameIndex;
	}

	// Find or add the item
	bool bAlreadyInTable;
	AnnouncedNames.FindOrAdd(NameIndex, true, &bAlreadyInTable);
	if (bAlreadyInTable)
	{
		return NameIndex;
	}

	// First time encountering this name, announce it
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
int32 MemoryTrace_AnnounceFNameTag(const FName& TagName)
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

