// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MassObserverManager.generated.h"


class UMassEntitySubsystem;
struct FMassArchetypeSubChunks;

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

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnObservedFragmentTypesChanged, const FMassFragmentBitSet& /*NewObservedAddSet*/, const FMassFragmentBitSet& /*NewObservedRemoveSet*/);

	FOnObservedFragmentTypesChanged& GetOnObservedFragmentTypesChangedDelegate() { return OnObservedFragmentTypesChangedDelegate; }

	const FMassFragmentBitSet& GetObservedAddFragmentsBitSet() const { return ObservedAddFragments; }
	const FMassFragmentBitSet& GetObservedRemoveFragmentsBitSet() const { return ObservedRemoveFragments; }

	bool HasOnAddedObserversForFragments(const FMassFragmentBitSet& InQueriedBitSet) const { return ObservedAddFragments.HasAny(InQueriedBitSet); }
	bool HasOnRemovedObserversForFragments(const FMassFragmentBitSet& InQueriedBitSet) const { return ObservedRemoveFragments.HasAny(InQueriedBitSet); }

	bool OnPostEntitiesCreated(const FMassArchetypeSubChunks& ChunkCollection);
	bool OnPostEntitiesCreated(FMassProcessingContext& ProcessingContext, const FMassArchetypeSubChunks& ChunkCollection);

	bool OnPreEntitiesDestroyed(const FMassArchetypeSubChunks& ChunkCollection);
	bool OnPreEntitiesDestroyed(FMassProcessingContext& ProcessingContext, const FMassArchetypeSubChunks& ChunkCollection);

	bool OnPostCompositionAdded(const FMassEntityHandle Entity, const FMassArchetypeCompositionDescriptor& Composition);
	bool OnPreCompositionRemoved(const FMassEntityHandle Entity, const FMassArchetypeCompositionDescriptor& Composition);

	void OnPostFragmentAdded(const UScriptStruct& FragmentType, const FMassArchetypeSubChunks& ChunkCollection)
	{
		HandleSingleFragmentImpl(FragmentType, ChunkCollection, ObservedAddFragments, OnFragmentAddedObservers);
	}

	void OnPreFragmentRemoved(const UScriptStruct& FragmentType, const FMassArchetypeSubChunks& ChunkCollection)
	{
		HandleSingleFragmentImpl(FragmentType, ChunkCollection, ObservedRemoveFragments, OnFragmentRemovedObservers);
	}

protected:
	friend UMassEntitySubsystem;
	explicit FMassObserverManager(UMassEntitySubsystem& Owner);

	void Initialize();
	void HandleFragmentsImpl(FMassProcessingContext& ProcessingContext, const FMassArchetypeSubChunks& ChunkCollection
		, const FMassFragmentBitSet& FragmentsBitSet, TMap<const UScriptStruct*, FMassRuntimePipeline>& HandlersContainer);
	void HandleSingleFragmentImpl(const UScriptStruct& FragmentType, const FMassArchetypeSubChunks& ChunkCollection
		, const FMassFragmentBitSet& FragmentFilterBitSet, TMap<const UScriptStruct*, FMassRuntimePipeline>& HandlersContainer);

	FOnObservedFragmentTypesChanged OnObservedFragmentTypesChangedDelegate;

	FMassFragmentBitSet ObservedAddFragments;
	FMassFragmentBitSet ObservedRemoveFragments;

	UPROPERTY()
	TMap<const UScriptStruct*, FMassRuntimePipeline> OnFragmentAddedObservers;

	UPROPERTY()
	TMap<const UScriptStruct*, FMassRuntimePipeline> OnFragmentRemovedObservers;

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