// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MassObserverManager.generated.h"


class UMassEntitySubsystem;
struct FMassArchetypeSubChunks;
class UMassProcessor;
struct FMassProcessingContext;

enum class FMassObservedOperation : uint8
{
	Add,
	Remove,
	// @todo Keeping this here as a indication of design intent. For now we handle entity destruction like removal, but 
	// there might be coputationaly expensive cases where we might want to avoid for soon-to-be-dead entities. 
	// Destroy,
	MAX
};

/** 
 * A wrapper type for a TMap to support having array-of-maps UPROPERTY members inFMassObserverManager
 */
USTRUCT()
struct FMassItemObserversMap
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

	const FMassFragmentBitSet& GetObservedAddFragmentsBitSet() const { return ObservedFragments[(uint8)FMassObservedOperation::Add]; }
	const FMassFragmentBitSet& GetObservedRemoveFragmentsBitSet() const { return ObservedFragments[(uint8)FMassObservedOperation::Remove]; }
	const FMassTagBitSet& GetObservedAddTagsBitSet() const { return ObservedTags[(uint8)FMassObservedOperation::Add]; }
	const FMassTagBitSet& GetObservedRemoveTagsBitSet() const { return ObservedTags[(uint8)FMassObservedOperation::Remove]; }

	bool HasOnAddedObserversForBitSet(const FMassFragmentBitSet& InQueriedBitSet) const { return ObservedFragments[(uint8)FMassObservedOperation::Add].HasAny(InQueriedBitSet); }
	bool HasOnAddedObserversForBitSet(const FMassTagBitSet& InQueriedBitSet) const { return ObservedTags[(uint8)FMassObservedOperation::Add].HasAny(InQueriedBitSet); }
	bool HasOnRemovedObserversForBitSet(const FMassFragmentBitSet& InQueriedBitSet) const { return ObservedFragments[(uint8)FMassObservedOperation::Remove].HasAny(InQueriedBitSet); }
	bool HasOnRemovedObserversForBitSet(const FMassTagBitSet& InQueriedBitSet) const { return ObservedTags[(uint8)FMassObservedOperation::Remove].HasAny(InQueriedBitSet); }

	bool HasObserversForBitSet(const FMassFragmentBitSet& InQueriedBitSet, const FMassObservedOperation Operation) const
	{
		return ObservedFragments[(uint8)Operation].HasAny(InQueriedBitSet);
	}

	bool HasObserversForBitSet(const FMassTagBitSet& InQueriedBitSet, const FMassObservedOperation Operation) const
	{
		return ObservedTags[(uint8)Operation].HasAny(InQueriedBitSet);
	}

	bool HasObserversForComposition(const FMassArchetypeCompositionDescriptor& Composition, const FMassObservedOperation Operation) const
	{
		return HasObserversForBitSet(Composition.Fragments, Operation) || HasObserversForBitSet(Composition.Tags, Operation);
	}

	bool OnPostEntitiesCreated(const FMassArchetypeSubChunks& ChunkCollection);
	bool OnPostEntitiesCreated(FMassProcessingContext& ProcessingContext, const FMassArchetypeSubChunks& ChunkCollection);

	bool OnPreEntitiesDestroyed(const FMassArchetypeSubChunks& ChunkCollection);
	bool OnPreEntitiesDestroyed(FMassProcessingContext& ProcessingContext, const FMassArchetypeSubChunks& ChunkCollection);

	bool OnCompositionChanged(const FMassEntityHandle Entity, const FMassArchetypeCompositionDescriptor& CompositionDelta, const FMassObservedOperation Operation);
	bool OnPostCompositionAdded(const FMassEntityHandle Entity, const FMassArchetypeCompositionDescriptor& Composition)
	{
		return OnCompositionChanged(Entity, Composition, FMassObservedOperation::Add);
	}
	bool OnPreCompositionRemoved(const FMassEntityHandle Entity, const FMassArchetypeCompositionDescriptor& Composition)
	{
		return OnCompositionChanged(Entity, Composition, FMassObservedOperation::Remove);
	}

	// @todo caller will need to recreate ChunkCollection if this call is done after modifications to entities, so it 
	// would be beneficial to ask the system "if there's anything observing the change" before actually reconstructing 
	// the chunk collection. Unless the new ChunkCollection gets created as a side-product of the batched modification 
	// operation
	bool OnCompositionChanged(const FMassArchetypeSubChunks& ChunkCollection, const FMassArchetypeCompositionDescriptor& Composition, const FMassObservedOperation Operation, FMassProcessingContext* InProcessingContext = nullptr);

	void OnPostItemAdded(const UScriptStruct& ItemType, const FMassArchetypeSubChunks& ChunkCollection)
	{
		OnSingleItemOperation(ItemType, ChunkCollection, FMassObservedOperation::Add);
	}
	void OnPreItemRemoved(const UScriptStruct& ItemType, const FMassArchetypeSubChunks& ChunkCollection)
	{
		OnSingleItemOperation(ItemType, ChunkCollection, FMassObservedOperation::Remove);
	}
	void OnSingleItemOperation(const UScriptStruct& ItemType, const FMassArchetypeSubChunks& ChunkCollection, const FMassObservedOperation Operation);

	void AddObserverInstance(const UScriptStruct& ItemType, const FMassObservedOperation Operation, UMassProcessor& ObserverProcessor);

protected:
	friend UMassEntitySubsystem;
	explicit FMassObserverManager(UMassEntitySubsystem& Owner);

	void Initialize();
	void HandleFragmentsImpl(FMassProcessingContext& ProcessingContext, const FMassArchetypeSubChunks& ChunkCollection
		, TArrayView<const UScriptStruct*> ObservedTypes, FMassItemObserversMap& HandlersContainer);
	void HandleSingleItemImpl(const UScriptStruct& FragmentType, const FMassArchetypeSubChunks& ChunkCollection, FMassItemObserversMap& HandlersContainer);

	FMassFragmentBitSet ObservedFragments[(uint8)FMassObservedOperation::MAX];
	FMassTagBitSet ObservedTags[(uint8)FMassObservedOperation::MAX];

	UPROPERTY()
	FMassItemObserversMap FragmentObservers[(uint8)FMassObservedOperation::MAX];

	UPROPERTY()
	FMassItemObserversMap TagObservers[(uint8)FMassObservedOperation::MAX];

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
