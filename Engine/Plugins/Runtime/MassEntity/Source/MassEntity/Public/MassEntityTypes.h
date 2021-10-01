// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScriptStructTypeBitSet.h"
#include "MassEntityTypes.generated.h"

// This is the base class for all lightweight components
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
// for the current world and can contain lightweight components.
USTRUCT()
struct FMassEntityHandle
{
	GENERATED_BODY()

	FMassEntityHandle() {}
	
	UPROPERTY(VisibleAnywhere, Category = "AggregateTicking|Debug", Transient)
	int32 Index = 0;
	
	UPROPERTY(VisibleAnywhere, Category = "AggregateTicking|Debug", Transient)
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
 *  components as well as tags */
struct FMassCompositionDescriptor
{
	FMassCompositionDescriptor() = default;
	FMassCompositionDescriptor(const FMassFragmentBitSet& InComponents, const FMassTagBitSet& InTags, const FMassChunkFragmentBitSet& InChunkComponents)
		: Components(InComponents), Tags(InTags), ChunkComponents(InChunkComponents)
	{}

	FMassCompositionDescriptor(TConstArrayView<const UScriptStruct*> InComponents, const FMassTagBitSet& InTags, const FMassChunkFragmentBitSet& InChunkComponents)
		: FMassCompositionDescriptor(FMassFragmentBitSet(InComponents), InTags, InChunkComponents)
	{}

	FMassCompositionDescriptor(TConstArrayView<FInstancedStruct> InComponentInstances, const FMassTagBitSet& InTags, const FMassChunkFragmentBitSet& InChunkComponents)
		: FMassCompositionDescriptor(FMassFragmentBitSet(InComponentInstances), InTags, InChunkComponents)
	{}

	FMassCompositionDescriptor(FMassFragmentBitSet&& InComponents, FMassTagBitSet&& InTags, FMassChunkFragmentBitSet&& InChunkComponents)
		: Components(MoveTemp(InComponents)), Tags(MoveTemp(InTags)), ChunkComponents(MoveTemp(InChunkComponents))
	{}

	void Reset()
	{
		Components.Reset();
		Tags.Reset();
	}

	bool IsEquivalent(const FMassFragmentBitSet& InComponentBitSet, const FMassTagBitSet& InTagBitSet, const FMassChunkFragmentBitSet& InChunkComponentsBitSet) const
	{
		return Components.IsEquivalent(InComponentBitSet) && Tags.IsEquivalent(InTagBitSet) && ChunkComponents.IsEquivalent(InChunkComponentsBitSet);
	}

	bool IsEquivalent(const FMassCompositionDescriptor& OtherDescriptor) const
	{
		return IsEquivalent(OtherDescriptor.Components, OtherDescriptor.Tags, OtherDescriptor.ChunkComponents);
	}

	bool IsEmpty() const { return Components.IsEmpty() && Tags.IsEmpty() && ChunkComponents.IsEmpty(); }

	static uint32 CalculateHash(const FMassFragmentBitSet& InComponents, const FMassTagBitSet& InTags, const FMassChunkFragmentBitSet& InChunkComponents)
	{
		const uint32 ComponentsHash = GetTypeHash(InComponents);
		const uint32 TagsHash = GetTypeHash(InTags);
		const uint32 ChunkComponentsHash = GetTypeHash(InChunkComponents);
		return HashCombine(HashCombine(ComponentsHash, TagsHash), ChunkComponentsHash);
	}	

	uint32 CalculateHash() const 
	{
		return CalculateHash(Components, Tags, ChunkComponents);
	}

	FMassFragmentBitSet Components;
	FMassTagBitSet Tags;
	FMassChunkFragmentBitSet ChunkComponents;
};
