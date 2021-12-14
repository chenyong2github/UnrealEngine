// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/ObjectPathId.h"
#include "Algo/FindLast.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/StringView.h"
#include "HAL/CriticalSection.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/Linker.h"
#include "UObject/Object.h"
#include "UObject/Package.h"

FCriticalSection GComplexPathLock;  // Could be changed to an RWLock later if needed

// @TODO: OBJPTR: Consider if it is possible to have this be case-preserving while still having equality checks between two paths of differing case be equal.
// @TODO: OBJPTR: Evaluate if the inline array for the object paths needs to be changed to something more lightweight.
// Currently each unique object path takes up 48 bytes of memory:
//  *8 bytes per entry in the ComplexPathHashToId map
//  *40 byte per entry in the ComplexPaths array
//My expectation is that a 3 name object path is generous in almost every case.  48 bytes
//per complex path may be too expensive depending on how frequently we encounter complex paths.
//If so, we can consider a packed pool to store the array elements in or other options for
//representing shared path elements like just registering the paths as FNames and not having
//our own storage at all.
TMultiMap<uint32, uint32> GComplexPathHashToId;
template class CORE_API TArray<FMinimalName, TInlineAllocator<3>>;
TArray<TArray<FMinimalName, TInlineAllocator<3>>> GComplexPaths;
/*
template <int a, int b>
struct assert_equality
{
	static_assert(a == b, "not equal");
};

//assert_equality<sizeof(TMultiMap<uint32, uint32>::ElementType), 0> A;
assert_equality<sizeof(TArray<FMinimalName, TInlineAllocator<3>>), 0> B;
*/

static inline uint64 NameToUInt64(const FName& Name)
{
	return (static_cast<uint64>(Name.GetComparisonIndex().ToUnstableInt()) << 32) |
			static_cast<uint64>(Name.GetNumber());
}

template <typename NameProducerType>
void StoreObjectPathId(NameProducerType& NameProducer, uint64 SimplePathFlag, uint64& OutObjectPathId)
{
	FName Name = NameProducer();

	if (Name == NAME_None)
	{
		return;
	}

	FName OuterName = NameProducer();
	if (OuterName == NAME_None)
	{
		int32 Number = Name.GetNumber();
		// Check if the number is in range by left shift + right shift
		if (static_cast<int32>((static_cast<uint32>(Number) << 1) >> 1) == Number)
		{
			//Simple path scenario
			OutObjectPathId = static_cast<uint64>(Name.GetComparisonIndex().ToUnstableInt()) << 32 |
								static_cast<uint64>(static_cast<uint32>(static_cast<uint32>(Number) << 1)) |
								SimplePathFlag;
			return;
		}
	}

	//Complex path scenario
	TArray<FMinimalName, TInlineAllocator<3>> MinimalNames;
	uint32 Key = GetTypeHash(NameToUInt64(Name));
	MinimalNames.Emplace(Name.GetComparisonIndex(), Name.GetNumber());
	while (OuterName != NAME_None)
	{
		Name = OuterName;
		OuterName = NameProducer();
		MinimalNames.Emplace(Name.GetComparisonIndex(), Name.GetNumber());
		Key = HashCombine(Key, GetTypeHash(NameToUInt64(Name)));
	}

	FScopeLock ComplexPathScopeLock(&GComplexPathLock);
	for (typename TMultiMap<uint32, uint32>::TConstKeyIterator It(GComplexPathHashToId, Key); It; ++It)
	{
		uint32 PotentialPathId = It.Value();
		if (GComplexPaths[PotentialPathId-1] == MinimalNames)
		{
			OutObjectPathId = static_cast<uint32>(PotentialPathId << 1);
			return;
		}
	}

	uint32 NewId = GComplexPaths.Add(MinimalNames) + 1;
	GComplexPathHashToId.Add(Key, NewId);
	OutObjectPathId = static_cast<uint32>(NewId << 1);
	GCoreComplexObjectPathDebug = GComplexPaths.GetData();
}

FObjectPathId::FObjectPathId(const UObject* Object)
{
	struct FLoadedObjectNamePathProducer
	{
		FLoadedObjectNamePathProducer(const UObject* InObject)
		: CurrentObject(InObject)
		{}

		FName operator()(void)
		{
			if ((CurrentObject == nullptr) || (CurrentObject->GetClass() == UPackage::StaticClass()))
			{
				return NAME_None;
			}
			FName RetVal = CurrentObject->GetFName();
			CurrentObject = CurrentObject->GetOuter();
			return RetVal;
		}
	private:
		const UObject* CurrentObject;
	} NamePathProducer(Object);

	StoreObjectPathId(NamePathProducer, static_cast<uint64>(EPathId::FlagSimple), PathId);
}

FObjectPathId::FObjectPathId(const FObjectImport& Import, const FLinkerTables& LinkerTables)
{
	MakeImportPathIdAndPackageName(Import, LinkerTables, *this);
}

FName FObjectPathId::MakeImportPathIdAndPackageName(const FObjectImport& Import, const FLinkerTables& LinkerTables, FObjectPathId& OutPathId)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FObjectPathId::MakeImportPathIdAndPackageName);
	// @TODO: OBJPTR: Need to handle redirects.  FCoreRedirectObjectName could be used, but it doesn't fit
	//		conveniently with the FName walk approach that is currently here. 
	struct FImportObjectNamePathProducer
	{
		FImportObjectNamePathProducer(const FObjectImport& InImport, const FLinkerTables& InLinkerTables)
		: CurrentImport(&InImport)
		, LinkerTables(InLinkerTables)
		{}

		FName operator()(void)
		{
			if ((CurrentImport == nullptr) || CurrentImport->OuterIndex.IsNull())
			{
				return NAME_None;
			}
			FName RetVal = CurrentImport->ObjectName;
			CurrentImport = &LinkerTables.Imp(CurrentImport->OuterIndex);
			return RetVal;
		}

		FName GetPackageName()
		{
			if (CurrentImport && CurrentImport->OuterIndex.IsNull())
			{
				return CurrentImport->ObjectName;
			}
			return NAME_None;
		}
	private:
		const FObjectImport* CurrentImport;
		const FLinkerTables& LinkerTables;
	} NamePathProducer(Import, LinkerTables);

	StoreObjectPathId(NamePathProducer, static_cast<uint64>(EPathId::FlagSimple), OutPathId.PathId);
	return NamePathProducer.GetPackageName();
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
template <typename CharType>
struct TStringViewNamePathProducer
{
	TStringViewNamePathProducer(TStringView<CharType> StringPath)
	: CurrentStringView(StringPath)
	{}

	FName operator()(void)
	{
		if (CurrentStringView.IsEmpty())
		{
			return NAME_None;
		}

		int32 FoundIndex = INDEX_NONE;
		if (!FindLastSeparator(FoundIndex))
		{
			FName RetVal(CurrentStringView);
			CurrentStringView.Reset();
			return RetVal;
		}

		FName RetVal(CurrentStringView.RightChop(FoundIndex + 1));
		CurrentStringView.RemoveSuffix(CurrentStringView.Len() - FoundIndex);
		return RetVal;
	}
private:
	static inline bool IsPathIdSeparator(CharType Char) { return Char == '.' || Char == ':'; }

	bool FindLastSeparator(int32& OutIndex)
	{
		if (const CharType* Separator = Algo::FindLastByPredicate(CurrentStringView, IsPathIdSeparator))
		{
			OutIndex = UE_PTRDIFF_TO_INT32(Separator - CurrentStringView.GetData());
			return true;
		}
		OutIndex = INDEX_NONE;
		return false;
	}

	TStringView<CharType> CurrentStringView;
};

FObjectPathId::FObjectPathId(FWideStringView StringPath)
{
	// @TODO: OBJPTR: Need to handle redirects.  FCoreRedirectObjectName could be used, but it doesn't fit
	//		conveniently with the FName walk approach that is currently here.
	TStringViewNamePathProducer<WIDECHAR> NamePathProducer(StringPath);

	StoreObjectPathId(NamePathProducer, static_cast<uint64>(EPathId::FlagSimple), PathId);
}

FObjectPathId::FObjectPathId(FAnsiStringView StringPath)
{
	// @TODO: OBJPTR: Need to handle redirects.  FCoreRedirectObjectName could be used, but it doesn't fit
	//		conveniently with the FName walk approach that is currently here.
	TStringViewNamePathProducer<ANSICHAR> NamePathProducer(StringPath);

	StoreObjectPathId(NamePathProducer, static_cast<uint64>(EPathId::FlagSimple), PathId);
}
#endif

void FObjectPathId::Resolve(ResolvedNameContainerType& OutContainer) const
{
	check(IsValid());
	
	if (IsNone())
	{
		return;
	}

	if (static_cast<uint64>(PathId) & static_cast<uint64>(EPathId::FlagSimple))
	{
		uint32 Number = static_cast<uint32>(PathId & 0x00000000FFFFFFE) >> 1;
		uint32 Index = static_cast<uint32>(PathId >> 32);
		FNameEntryId EntryId = FNameEntryId::FromUnstableInt(Index);
		OutContainer.Emplace(EntryId, EntryId, Number);
		return; 
	}

	FScopeLock ComplexPathScopeLock(&GComplexPathLock);
	//Append reversed path
	TArray<FMinimalName, TInlineAllocator<3>>& FoundContainer = GComplexPaths[(PathId >> 1) - 1];
	OutContainer.Reserve(OutContainer.Num() + FoundContainer.Num());
	for (int32 Index = FoundContainer.Num() - 1; Index >= 0; --Index)
	{
		FMinimalName& FoundMinimalName = FoundContainer[Index];
		OutContainer.Emplace(FoundMinimalName.Index, FoundMinimalName.Index, FoundMinimalName.Number);
	}
}
