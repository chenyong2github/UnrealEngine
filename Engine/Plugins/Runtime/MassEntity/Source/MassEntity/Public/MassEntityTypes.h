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

/** The type summarily describing a composition of an entity or an archetype. It contains information on both the
 *  fragments as well as tags */
struct FMassArchetypeCompositionDescriptor
{
	FMassArchetypeCompositionDescriptor() = default;
	FMassArchetypeCompositionDescriptor(const FMassFragmentBitSet& InFragments, const FMassTagBitSet& InTags, const FMassChunkFragmentBitSet& InChunkFragments)
		: Fragments(InFragments), Tags(InTags), ChunkFragments(InChunkFragments)
	{}

	FMassArchetypeCompositionDescriptor(TConstArrayView<const UScriptStruct*> InFragments, const FMassTagBitSet& InTags, const FMassChunkFragmentBitSet& InChunkFragments)
		: FMassArchetypeCompositionDescriptor(FMassFragmentBitSet(InFragments), InTags, InChunkFragments)
	{}

	FMassArchetypeCompositionDescriptor(TConstArrayView<FInstancedStruct> InFragmentInstances, const FMassTagBitSet& InTags, const FMassChunkFragmentBitSet& InChunkFragments)
		: FMassArchetypeCompositionDescriptor(FMassFragmentBitSet(InFragmentInstances), InTags, InChunkFragments)
	{}

	FMassArchetypeCompositionDescriptor(FMassFragmentBitSet&& InFragments, FMassTagBitSet&& InTags, FMassChunkFragmentBitSet&& InChunkFragments)
		: Fragments(MoveTemp(InFragments)), Tags(MoveTemp(InTags)), ChunkFragments(MoveTemp(InChunkFragments))
	{}

	void Reset()
	{
		Fragments.Reset();
		Tags.Reset();
		ChunkFragments.Reset();
	}

	bool IsEquivalent(const FMassFragmentBitSet& InFragmentBitSet, const FMassTagBitSet& InTagBitSet, const FMassChunkFragmentBitSet& InChunkFragmentsBitSet) const
	{
		return Fragments.IsEquivalent(InFragmentBitSet) && Tags.IsEquivalent(InTagBitSet) && ChunkFragments.IsEquivalent(InChunkFragmentsBitSet);
	}

	bool IsEquivalent(const FMassArchetypeCompositionDescriptor& OtherDescriptor) const
	{
		return IsEquivalent(OtherDescriptor.Fragments, OtherDescriptor.Tags, OtherDescriptor.ChunkFragments);
	}

	bool IsEmpty() const { return Fragments.IsEmpty() && Tags.IsEmpty() && ChunkFragments.IsEmpty(); }

	static uint32 CalculateHash(const FMassFragmentBitSet& InFragments, const FMassTagBitSet& InTags, const FMassChunkFragmentBitSet& InChunkFragments)
	{
		const uint32 FragmentsHash = GetTypeHash(InFragments);
		const uint32 TagsHash = GetTypeHash(InTags);
		const uint32 ChunkFragmentsHash = GetTypeHash(InChunkFragments);
		return HashCombine(HashCombine(FragmentsHash, TagsHash), ChunkFragmentsHash);
	}	

	uint32 CalculateHash() const 
	{
		return CalculateHash(Fragments, Tags, ChunkFragments);
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
};

struct FMassArchetypeFragmentsInitialValues
{
	FMassArchetypeFragmentsInitialValues() = default;

	TArray<FInstancedStruct> Fragments;
	TArray<FInstancedStruct> ChunkFragments;

	// @todo add the shared fragments values here
};