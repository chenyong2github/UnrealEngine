// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScriptStructTypeBitSet.h"
#include "MassProcessingTypes.h"
#include "MassEntityTypes.generated.h"


MASSENTITY_API DECLARE_LOG_CATEGORY_EXTERN(LogMass, Warning, All);

// This is the base class for all lightweight fragments
USTRUCT()
struct FMassFragment
{
	GENERATED_BODY()

	FMassFragment() {}
};

// This is the base class for types that will only be tested for presence/absence, i.e. Tags.
// Subclasses should never contain any member properties.
USTRUCT()
struct FMassTag
{
	GENERATED_BODY()

	FMassTag() {}
};

USTRUCT()
struct FMassChunkFragment
{
	GENERATED_BODY()

	FMassChunkFragment() {}
};

USTRUCT()
struct FMassSharedFragment
{
	GENERATED_BODY()

	FMassSharedFragment() {}
};

// A handle to a lightweight entity.  An entity is used in conjunction with the UMassEntitySubsystem
// for the current world and can contain lightweight fragments.
USTRUCT()
struct FMassEntityHandle
{
	GENERATED_BODY()

	FMassEntityHandle()
	{
	}
	FMassEntityHandle(const int32 InIndex, const int32 InSerialNumber)
		: Index(InIndex), SerialNumber(InSerialNumber)
	{
	}
	
	UPROPERTY(VisibleAnywhere, Category = "Mass|Debug", Transient)
	int32 Index = 0;
	
	UPROPERTY(VisibleAnywhere, Category = "Mass|Debug", Transient)
	int32 SerialNumber = 0;

	bool operator==(const FMassEntityHandle Other) const
	{
		return Index == Other.Index && SerialNumber == Other.SerialNumber;
	}

	bool operator!=(const FMassEntityHandle Other) const
	{
		return !operator==(Other);
	}

	/** Note that this function is merely checking if Index and SerialNumber are set. There's no way to validate if 
	 *  these indicate a valid entity in an EntitySubsystem without asking the system. */
	bool IsSet() const
	{
		return Index != 0 && SerialNumber != 0;
	}

	FORCEINLINE bool IsValid() const
	{
		return IsSet();
	}

	void Reset()
	{
		Index = SerialNumber = 0;
	}

	friend uint32 GetTypeHash(const FMassEntityHandle Entity)
	{
		return HashCombine(Entity.Index, Entity.SerialNumber);
	}

	FString DebugGetDescription() const
	{
		return FString::Printf(TEXT("i: %d sn: %d"), Index, SerialNumber);
	}
};

template struct MASSENTITY_API TScriptStructTypeBitSet<FMassFragment>;
using FMassFragmentBitSet = TScriptStructTypeBitSet<FMassFragment>;
template struct MASSENTITY_API TScriptStructTypeBitSet<FMassTag>;
using FMassTagBitSet = TScriptStructTypeBitSet<FMassTag>;
template struct MASSENTITY_API TScriptStructTypeBitSet<FMassChunkFragment>;
using FMassChunkFragmentBitSet = TScriptStructTypeBitSet<FMassChunkFragment>;
template struct MASSENTITY_API TScriptStructTypeBitSet<FMassSharedFragment>;
using FMassSharedFragmentBitSet = TScriptStructTypeBitSet<FMassSharedFragment>;

/** The type summarily describing a composition of an entity or an archetype. It contains information on both the
 *  fragments as well as tags */
struct FMassArchetypeCompositionDescriptor
{
	FMassArchetypeCompositionDescriptor() = default;
	FMassArchetypeCompositionDescriptor(const FMassFragmentBitSet& InFragments, const FMassTagBitSet& InTags, const FMassChunkFragmentBitSet& InChunkFragments, const FMassSharedFragmentBitSet& InSharedFragments)
		: Fragments(InFragments)
		, Tags(InTags)
		, ChunkFragments(InChunkFragments)
		, SharedFragments(InSharedFragments)
	{}

	FMassArchetypeCompositionDescriptor(TConstArrayView<const UScriptStruct*> InFragments, const FMassTagBitSet& InTags, const FMassChunkFragmentBitSet& InChunkFragments, const FMassSharedFragmentBitSet& InSharedFragments)
		: FMassArchetypeCompositionDescriptor(FMassFragmentBitSet(InFragments), InTags, InChunkFragments, InSharedFragments)
	{}

	FMassArchetypeCompositionDescriptor(TConstArrayView<FInstancedStruct> InFragmentInstances, const FMassTagBitSet& InTags, const FMassChunkFragmentBitSet& InChunkFragments, const FMassSharedFragmentBitSet& InSharedFragments)
		: FMassArchetypeCompositionDescriptor(FMassFragmentBitSet(InFragmentInstances), InTags, InChunkFragments, InSharedFragments)
	{}

	FMassArchetypeCompositionDescriptor(FMassFragmentBitSet&& InFragments, FMassTagBitSet&& InTags, FMassChunkFragmentBitSet&& InChunkFragments, FMassSharedFragmentBitSet&& InSharedFragments)
		: Fragments(MoveTemp(InFragments))
		, Tags(MoveTemp(InTags))
		, ChunkFragments(MoveTemp(InChunkFragments))
		, SharedFragments(MoveTemp(InSharedFragments))
	{}

	void Reset()
	{
		Fragments.Reset();
		Tags.Reset();
		ChunkFragments.Reset();
	}

	bool IsEquivalent(const FMassArchetypeCompositionDescriptor& OtherDescriptor) const
	{
		return Fragments.IsEquivalent(OtherDescriptor.Fragments) &&
			Tags.IsEquivalent(OtherDescriptor.Tags) &&
			ChunkFragments.IsEquivalent(OtherDescriptor.ChunkFragments) &&
			SharedFragments.IsEquivalent(OtherDescriptor.SharedFragments);
	}

	bool IsEmpty() const 
	{ 
		return Fragments.IsEmpty() &&
			Tags.IsEmpty() &&
			ChunkFragments.IsEmpty() &&
			SharedFragments.IsEmpty();
	}

	static uint32 CalculateHash(const FMassFragmentBitSet& InFragments, const FMassTagBitSet& InTags, const FMassChunkFragmentBitSet& InChunkFragments, const FMassSharedFragmentBitSet& InSharedFragmentBitSet)
	{
		const uint32 FragmentsHash = GetTypeHash(InFragments);
		const uint32 TagsHash = GetTypeHash(InTags);
		const uint32 ChunkFragmentsHash = GetTypeHash(InChunkFragments);
		const uint32 SharedFragmentsHash = GetTypeHash(InSharedFragmentBitSet);
		return HashCombine(HashCombine(HashCombine(FragmentsHash, TagsHash), ChunkFragmentsHash), SharedFragmentsHash);
	}	

	uint32 CalculateHash() const 
	{
		return CalculateHash(Fragments, Tags, ChunkFragments, SharedFragments);
	}

	void DebugOutputDescription(FOutputDevice& Ar) const
	{
#if WITH_MASSENTITY_DEBUG
		const bool bAutoLineEnd = Ar.GetAutoEmitLineTerminator();
		Ar.SetAutoEmitLineTerminator(false);

		Ar.Logf(TEXT("Fragments:\n"));
		Fragments.DebugGetStringDesc(Ar);
		Ar.Logf(TEXT("Tags:\n"));
		Tags.DebugGetStringDesc(Ar);
		Ar.Logf(TEXT("ChunkFragments:\n"));
		ChunkFragments.DebugGetStringDesc(Ar);

		Ar.SetAutoEmitLineTerminator(bAutoLineEnd);
#endif // WITH_MASSENTITY_DEBUG

	}

	FMassFragmentBitSet Fragments;
	FMassTagBitSet Tags;
	FMassChunkFragmentBitSet ChunkFragments;
	FMassSharedFragmentBitSet SharedFragments;
};

struct FMassArchetypeFragmentsInitialValues
{
	FMassArchetypeFragmentsInitialValues() = default;

	FORCEINLINE bool IsEquivalent(const FMassArchetypeFragmentsInitialValues& OtherInitialValues) const
	{
		return GetTypeHash(*this) == GetTypeHash(OtherInitialValues);
	}

	template<typename T>
	FORCEINLINE T& AddFragment(const T& Fragment)
	{
		DirtyHashCache();
		return Fragments.Add_GetRef(FConstStructView::Make(Fragment)).template GetMutable<T>();
	}

	FORCEINLINE void AddFragment(FConstStructView Fragment)
	{
		DirtyHashCache();
		Fragments.Add(Fragment);
	}

	template<typename T>
	FORCEINLINE T& AddChunkFragment(const T& Fragment)
	{
		DirtyHashCache();
		return ChunkFragments.Add_GetRef(FConstStructView::Make(Fragment)).template GetMutable<T>();
	}

	FORCEINLINE void AddChunkFragment(FConstStructView Fragment)
	{
		DirtyHashCache();
		ChunkFragments.Add(Fragment);
	}

	FORCEINLINE FConstSharedStruct& AddConstSharedFragment(const FConstSharedStruct& Fragment)
	{
		DirtyHashCache();
		return ConstSharedFragments.Add_GetRef(Fragment);
	}

	FORCEINLINE FSharedStruct AddSharedFragment(const FSharedStruct& Fragment)
	{
		DirtyHashCache();
		return SharedFragments.Add_GetRef(Fragment);
	}

	FORCEINLINE const TArray<FInstancedStruct>& GetFragments() const
	{
		return Fragments;
	}

	FORCEINLINE const TArray<FInstancedStruct>& GetChunkFragments() const
	{
		return ChunkFragments;
	}

	FORCEINLINE const TArray<FConstSharedStruct>& GetConstSharedFragments() const
	{
		return ConstSharedFragments;
	}

	FORCEINLINE const TArray<FSharedStruct>& GetSharedFragments() const
	{
		return SharedFragments;
	}

	FORCEINLINE void SetFragments(const TArray<FInstancedStruct>& InFragments)
	{
		DirtyHashCache();
		Fragments = InFragments;
	}

	FORCEINLINE void SetChunkFragments(const TArray<FInstancedStruct>& InChunkFragments)
	{
		DirtyHashCache();
		ChunkFragments = InChunkFragments;
	}

	FORCEINLINE void SetConstSharedFragments(const TArray<FConstSharedStruct>& InConstSharedFragments)
	{
		DirtyHashCache();
		ConstSharedFragments = InConstSharedFragments;
	}

	FORCEINLINE void SetSharedFragments(const TArray<FSharedStruct>& InSharedFragments)
	{
		DirtyHashCache();
		SharedFragments = InSharedFragments;
	}

	FORCEINLINE void DirtyHashCache()
	{
		HashCache = UINT32_MAX;
	}

	FORCEINLINE void CacheHash() const
	{
		if (HashCache == UINT32_MAX)
		{
			HashCache = CalculateHash();
		}
	}

	friend FORCEINLINE uint32 GetTypeHash(const FMassArchetypeFragmentsInitialValues& InitialValues)
	{
		InitialValues.CacheHash();
		return InitialValues.HashCache;
	}

	uint32 CalculateHash() const;

	SIZE_T GetAllocatedSize() const
	{
		return Fragments.GetAllocatedSize() + ChunkFragments.GetAllocatedSize() + ConstSharedFragments.GetAllocatedSize() + SharedFragments.GetAllocatedSize();
	}

protected:
	mutable uint32 HashCache = UINT32_MAX;
	// Atodo remove the fragments and ChunkFragments as they are not used and it creating inconsistency in the uniqueness of the Archetypes.
	TArray<FInstancedStruct> Fragments;
	TArray<FInstancedStruct> ChunkFragments;
	TArray<FConstSharedStruct> ConstSharedFragments;
	TArray<FSharedStruct> SharedFragments;
};