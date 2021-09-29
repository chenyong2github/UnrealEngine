// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScriptStructTypeBitSet.h"
#include "LWComponentTypes.generated.h"

// the name is temporary. To be changed when we arrive at the final name for the plugin
#define WITH_AGGREGATETICKING_DEBUG (!(UE_BUILD_SHIPPING || UE_BUILD_SHIPPING_WITH_EDITOR || UE_BUILD_TEST) && WITH_STRUCTUTILS_DEBUG && 1)

// This is the base class for all lightweight components
USTRUCT()
struct FLWComponentData
{
	GENERATED_BODY()

	FLWComponentData() {}
};

// This is the base class for types that will only be tested for presence/absence, i.e. Tags.
// Subclasses should never contain any member properties.
USTRUCT()
struct FComponentTag
{
	GENERATED_BODY()

	FComponentTag() {}
};

USTRUCT()
struct FLWChunkComponent
{
	GENERATED_BODY()

	FLWChunkComponent() {}
};

// A handle to a lightweight entity.  An entity is used in conjunction with the UEntitySubsystem
// for the current world and can contain lightweight components.
USTRUCT()
struct FLWEntity
{
	GENERATED_BODY()

	FLWEntity() {}
	
	UPROPERTY(VisibleAnywhere, Category = "AggregateTicking|Debug", Transient)
	int32 Index = 0;
	
	UPROPERTY(VisibleAnywhere, Category = "AggregateTicking|Debug", Transient)
	int32 SerialNumber = 0;

	bool operator==(const FLWEntity& Other) const
	{
		return Index == Other.Index && SerialNumber == Other.SerialNumber;
	}

	bool operator!=(const FLWEntity& Other) const
	{
		return !operator==(Other);
	}

	/** Note that this function is merely checking if Index and SerialNumber are set. There's no way to validate if 
	 *  these indicate a valid entity in an EntitySubsystem without asking the system. */
	bool IsSet() const
	{
		return Index != 0 && SerialNumber != 0;
	}

	void Reset()
	{
		Index = SerialNumber = 0;
	}

	friend uint32 GetTypeHash(const FLWEntity& Entity)
	{
		return HashCombine(Entity.Index, Entity.SerialNumber);
	}

	FString DebugGetDescription() const
	{
		return FString::Printf(TEXT("i: %d sn: %d"), Index, SerialNumber);
	}
};

template struct MASSENTITY_API TScriptStructTypeBitSet<FLWComponentData>;
using FLWComponentBitSet = TScriptStructTypeBitSet<FLWComponentData>;
template struct MASSENTITY_API TScriptStructTypeBitSet<FComponentTag>;
using FLWTagBitSet = TScriptStructTypeBitSet<FComponentTag>;
template struct MASSENTITY_API TScriptStructTypeBitSet<FLWChunkComponent>;
using FLWChunkComponentBitSet = TScriptStructTypeBitSet<FLWChunkComponent>;

/** The type summarily describing a composition of an entity or an archetype. It contains information on both the
 *  components as well as tags */
struct FLWCompositionDescriptor
{
	FLWCompositionDescriptor() = default;
	FLWCompositionDescriptor(const FLWComponentBitSet& InComponents, const FLWTagBitSet& InTags, const FLWChunkComponentBitSet& InChunkComponents)
		: Components(InComponents), Tags(InTags), ChunkComponents(InChunkComponents)
	{}

	FLWCompositionDescriptor(TConstArrayView<const UScriptStruct*> InComponents, const FLWTagBitSet& InTags, const FLWChunkComponentBitSet& InChunkComponents)
		: FLWCompositionDescriptor(FLWComponentBitSet(InComponents), InTags, InChunkComponents)
	{}

	FLWCompositionDescriptor(TConstArrayView<FInstancedStruct> InComponentInstances, const FLWTagBitSet& InTags, const FLWChunkComponentBitSet& InChunkComponents)
		: FLWCompositionDescriptor(FLWComponentBitSet(InComponentInstances), InTags, InChunkComponents)
	{}

	FLWCompositionDescriptor(FLWComponentBitSet&& InComponents, FLWTagBitSet&& InTags, FLWChunkComponentBitSet&& InChunkComponents)
		: Components(MoveTemp(InComponents)), Tags(MoveTemp(InTags)), ChunkComponents(MoveTemp(InChunkComponents))
	{}

	void Reset()
	{
		Components.Reset();
		Tags.Reset();
	}

	bool IsEquivalent(const FLWComponentBitSet& InComponentBitSet, const FLWTagBitSet& InTagBitSet, const FLWChunkComponentBitSet& InChunkComponentsBitSet) const
	{
		return Components.IsEquivalent(InComponentBitSet) && Tags.IsEquivalent(InTagBitSet) && ChunkComponents.IsEquivalent(InChunkComponentsBitSet);
	}

	bool IsEquivalent(const FLWCompositionDescriptor& OtherDescriptor) const
	{
		return IsEquivalent(OtherDescriptor.Components, OtherDescriptor.Tags, OtherDescriptor.ChunkComponents);
	}

	bool IsEmpty() const { return Components.IsEmpty() && Tags.IsEmpty() && ChunkComponents.IsEmpty(); }

	static uint32 CalculateHash(const FLWComponentBitSet& InComponents, const FLWTagBitSet& InTags, const FLWChunkComponentBitSet& InChunkComponents)
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

	FLWComponentBitSet Components;
	FLWTagBitSet Tags;
	FLWChunkComponentBitSet ChunkComponents;
};
