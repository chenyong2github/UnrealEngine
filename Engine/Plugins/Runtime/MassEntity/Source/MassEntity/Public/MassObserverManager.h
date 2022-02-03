// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MassObserverManager.generated.h"


class UMassEntitySubsystem;
struct FMassArchetypeSubChunks;
class UMassProcessor;
struct FMassProcessingContext;

/** 
 * A wrapper type for a TMap to support having array-of-maps UPROPERTY members in FMassObserverManager
 */
USTRUCT()
struct FMassObserversMap
{
	GENERATED_BODY()

	// a helper accessor simplifying access while still keeping Container private
	TMap<const UScriptStruct*, FMassRuntimePipeline>& operator*()
	{
		return Container;
	}

private:
	UPROPERTY()
	TMap<const UScriptStruct*, FMassRuntimePipeline> Container;
};

/** 
 * A type that encapsulates logic related to notifying interested parties of entity composition changes. Upon creation it
 * reads information from UMassObserverRegistry and instantiates processors interested in handling given fragment
 * type addition or removal.
 */
USTRUCT()
struct MASSENTITY_API FMassObserverManager
{
	GENERATED_BODY()

public:
	FMassObserverManager();	

	const FMassFragmentBitSet* GetObservedFragmentBitSets() const { return ObservedFragments; }
	const FMassFragmentBitSet& GetObservedFragmentsBitSet(const EMassObservedOperation Operation) const 
	{ 
		return ObservedFragments[(uint8)Operation]; 
	}

	const FMassTagBitSet* GetObservedTagBitSets() const { return ObservedTags; }
	const FMassTagBitSet& GetObservedTagsBitSet(const EMassObservedOperation Operation) const 
	{ 
		return ObservedTags[(uint8)Operation]; 
	}
	
	bool HasObserversForBitSet(const FMassFragmentBitSet& InQueriedBitSet, const EMassObservedOperation Operation) const
	{
		return ObservedFragments[(uint8)Operation].HasAny(InQueriedBitSet);
	}

	bool HasObserversForBitSet(const FMassTagBitSet& InQueriedBitSet, const EMassObservedOperation Operation) const
	{
		return ObservedTags[(uint8)Operation].HasAny(InQueriedBitSet);
	}

	bool HasObserversForComposition(const FMassArchetypeCompositionDescriptor& Composition, const EMassObservedOperation Operation) const
	{
		return HasObserversForBitSet(Composition.Fragments, Operation) || HasObserversForBitSet(Composition.Tags, Operation);
	}

	bool OnPostEntitiesCreated(const FMassArchetypeSubChunks& ChunkCollection);
	bool OnPostEntitiesCreated(FMassProcessingContext& ProcessingContext, const FMassArchetypeSubChunks& ChunkCollection);

	bool OnPreEntitiesDestroyed(const FMassArchetypeSubChunks& ChunkCollection);
	bool OnPreEntitiesDestroyed(FMassProcessingContext& ProcessingContext, const FMassArchetypeSubChunks& ChunkCollection);

	bool OnCompositionChanged(const FMassEntityHandle Entity, const FMassArchetypeCompositionDescriptor& CompositionDelta, const EMassObservedOperation Operation);
	bool OnPostCompositionAdded(const FMassEntityHandle Entity, const FMassArchetypeCompositionDescriptor& Composition)
	{
		return OnCompositionChanged(Entity, Composition, EMassObservedOperation::Add);
	}
	bool OnPreCompositionRemoved(const FMassEntityHandle Entity, const FMassArchetypeCompositionDescriptor& Composition)
	{
		return OnCompositionChanged(Entity, Composition, EMassObservedOperation::Remove);
	}

	bool OnCompositionChanged(const FMassArchetypeSubChunks& ChunkCollection, const FMassArchetypeCompositionDescriptor& Composition, const EMassObservedOperation Operation, FMassProcessingContext* InProcessingContext = nullptr);

	void OnPostFragmentOrTagAdded(const UScriptStruct& FragmentOrTagType, const FMassArchetypeSubChunks& ChunkCollection)
	{
		OnFragmentOrTagOperation(FragmentOrTagType, ChunkCollection, EMassObservedOperation::Add);
	}
	void OnPreFragmentOrTagRemoved(const UScriptStruct& FragmentOrTagType, const FMassArchetypeSubChunks& ChunkCollection)
	{
		OnFragmentOrTagOperation(FragmentOrTagType, ChunkCollection, EMassObservedOperation::Remove);
	}
	// @todo I don't love this name. Alternatively could be OnSingleFragmentOrTagOperation. Long term we'll switch to
	// OnSingleFragmentOperation and advertise that Tags are a type of Fragment as well (conceptually).
	void OnFragmentOrTagOperation(const UScriptStruct& FragmentOrTagType, const FMassArchetypeSubChunks& ChunkCollection, const EMassObservedOperation Operation);

	void AddObserverInstance(const UScriptStruct& FragmentOrTagType, const EMassObservedOperation Operation, UMassProcessor& ObserverProcessor);

protected:
	friend UMassEntitySubsystem;
	explicit FMassObserverManager(UMassEntitySubsystem& Owner);

	void Initialize();
	void HandleFragmentsImpl(FMassProcessingContext& ProcessingContext, const FMassArchetypeSubChunks& ChunkCollection
		, TArrayView<const UScriptStruct*> ObservedTypes, FMassObserversMap& HandlersContainer);
	void HandleSingleEntityImpl(const UScriptStruct& FragmentType, const FMassArchetypeSubChunks& ChunkCollection, FMassObserversMap& HandlersContainer);

	FMassFragmentBitSet ObservedFragments[(uint8)EMassObservedOperation::MAX];
	FMassTagBitSet ObservedTags[(uint8)EMassObservedOperation::MAX];

	UPROPERTY()
	FMassObserversMap FragmentObservers[(uint8)EMassObservedOperation::MAX];

	UPROPERTY()
	FMassObserversMap TagObservers[(uint8)EMassObservedOperation::MAX];

	/** 
	 * The owning EntitySubsystem. No need for it to be a UPROPERTY since by design we don't support creation of 
	 * FMassObserverManager outside of an UMassEntitySubsystem instance 
	 */
	UMassEntitySubsystem& EntitySubsystem;
};

template<>
struct TStructOpsTypeTraits<FMassObserverManager> : public TStructOpsTypeTraitsBase2<FMassObserverManager>
{
	enum
	{
		WithCopy = false,
	};
};
