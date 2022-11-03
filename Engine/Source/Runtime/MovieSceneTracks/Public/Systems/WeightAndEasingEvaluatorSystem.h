// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/ContainerAllocationPolicies.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Containers/SortedMap.h"
#include "Containers/SparseArray.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/EntityAllocationIterator.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieSceneEntityInstantiatorSystem.h"
#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/MovieSceneSequenceInstanceHandle.h"
#include "HAL/Platform.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "WeightAndEasingEvaluatorSystem.generated.h"

class UMovieSceneEntitySystemLinker;
class UMovieSceneEvalTimeSystem;
class UWeightAndEasingEvaluatorSystem;
class UObject;
struct FMovieSceneSubSequenceData;

namespace UE::MovieScene
{

struct FAddEasingChannelToProviderMutation;

/** Computation data used for accumulating hierarchical weights for sub sequences */
struct FHierarchicalEasingChannelData
{
	/** Our parent's computation data within PreAllocatedComputationData. Must only access if bResultsNeedResort is false. */
	uint16 ParentEasingIndex = uint16(-1);
	/** The accumulated hierarchical depth of this sequence within its root. */
	uint16 HierarchicalDepth = 0;
	/**
	 * The easing channel ID for this data. This reprensents the index within EasingChannelToIndex that uniquely identifies our channel.
	 * Any entity within this sequence that is subject to easing will contain a HierarchicalEasingChannel component with this ID
	 */
	uint16 ChannelID = uint16(-1);
	/** The final result of this easing channel, accumulated with all parents */
	double FinalResult = 1.0;
};

}  // namespace UE::MovieScene

/**
 * System that creates hierarchical easing channels for any newly introduced HierarchicalEasingProvider components
 */
UCLASS()
class MOVIESCENETRACKS_API UMovieSceneHierarchicalEasingInstantiatorSystem : public UMovieSceneEntityInstantiatorSystem
{
public:

	struct FHierarchicalInstanceData
	{
		int32 HierarchicalDepth = -1;
		uint16 RefCount = 0;
		uint16 EasingChannelID = uint16(-1);
	};

	GENERATED_BODY()

	UMovieSceneHierarchicalEasingInstantiatorSystem(const FObjectInitializer& ObjInit);

	/** 
	 * Locate an already allocated easing channel for the specified sequence instance and sub sequence ID.
	 *
	 * @param RootInstanceHandle The instance handle of the root sequence
	 * @param SequenceID         The Sequence ID of the sub sequence
	 * @return The sequence's easing channel or 0xFFFF if one does not exist
	 */
	uint16 LocateEasingChannel(UE::MovieScene::FRootInstanceHandle RootInstanceHandle, FMovieSceneSequenceID SequenceID) const;

	/**
	 * Remove any hierarchical easing channels for channels that are no longer needed because of unlinked easing providers
	 */
	void RemoveUnlinkedHierarchicalEasingChannels(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents);

private:

	virtual bool IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const;
	virtual void OnLink() override;
	virtual void OnUnlink() override;
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;

private:

	/** 
	 * Allocate a new easing channel for the specified sequence instance and sub sequence ID. Will return an existing channel if it has already been allocated.
	 *
	 * @param InstanceRegistry   Pointer to the instance registry for retrievable of sequence instances (see UMovieSceneEntitySystemLinbker::GetInstanceRegistry)
	 * @param RootInstanceHandle The instance handle of the root sequence
	 * @param SequenceID         The Sequence ID of the sub sequence to make a channel for, or MovieSceneSequenceID::Root for the root sequence
	 * @return Structure containing both the easing channel ID and this sequence's hierarchical depth
	 */
	FHierarchicalInstanceData AllocateEasingChannel(const UE::MovieScene::FInstanceRegistry* InstanceRegistry, UE::MovieScene::FRootInstanceHandle RootInstanceHandle, FMovieSceneSequenceID SequenceID);

	/** 
	 * Release an easing channel for the specified sequence instance and sub sequence ID if one exists
	 *
	 * @param RootInstanceHandle The instance handle of the root sequence
	 * @param SequenceID         The Sequence ID of the sub sequence
	 */
	void ReleaseEasingChannel(UE::MovieScene::FRootInstanceHandle RootInstanceHandle, FMovieSceneSequenceID SequenceID);

	/** 
	 * Allocate a new easing channel for the specified sequence instance and sub sequence ID, and all its parents. Will return an existing channel if it has already been allocated.
	 *
	 * @param RootInstanceHandle The instance handle of the root sequence
	 * @param SequenceID         The Sequence ID of the sub sequence to make a channel for, or MovieSceneSequenceID::Root for the root sequence
	 * @param Hierarchy          The sequence hierarchy (or nullptr if this is a root sequence and one is not available)
	 * @return Structure containing both the easing channel ID and this sequence's hierarchical depth
	 */
	FHierarchicalInstanceData AllocateEasingChannelImpl(UE::MovieScene::FRootInstanceHandle RootInstanceHandle, FMovieSceneSequenceID SequenceID, const FMovieSceneSequenceHierarchy* Hierarchy);

private:

	friend UE::MovieScene::FAddEasingChannelToProviderMutation;

	/** Hierarchy key for associating channel IDs to pairs of instance handles and sequence IDs */
	struct FHierarchicalKey
	{
		UE::MovieScene::FRootInstanceHandle RootInstanceHandle;
		FMovieSceneSequenceID SequenceID;

		friend uint32 GetTypeHash(const FHierarchicalKey& In)
		{
			return HashCombine(GetTypeHash(In.RootInstanceHandle), GetTypeHash(In.SequenceID));
		}
		friend bool operator==(const FHierarchicalKey& A, const FHierarchicalKey& B)
		{
			return A.RootInstanceHandle == B.RootInstanceHandle && A.SequenceID == B.SequenceID;
		}
	};

	/** Map between a sub-sequence handle and the easing channel affecting it */
	TMap<FHierarchicalKey, FHierarchicalInstanceData> PersistentHandleToEasingChannel;
	/** Set of newly created easing channels this frame. This is used to add easing data to existing entities. */
	TSet<FHierarchicalKey> NewEasingChannelKeys;

	UPROPERTY()
	TObjectPtr<UWeightAndEasingEvaluatorSystem> EvaluatorSystem;
};

/**
 * System that combines manual weights and easings and propagates them to entities with matching EasingChannelID components
 */
UCLASS()
class MOVIESCENETRACKS_API UWeightAndEasingEvaluatorSystem : public UMovieSceneEntitySystem
{
public:

	GENERATED_BODY()

	UWeightAndEasingEvaluatorSystem(const FObjectInitializer& ObjInit);

	uint16 AllocateEasingChannel(const uint16 ParentEasingChannel, const uint16 HierarchicaDepth);
	void ReleaseEasingChannel(uint16 EasingChannelID);

private:

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;
	virtual bool IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const override;
	virtual void OnLink() override final;
	virtual void OnUnlink() override final;

	void ResortComputationBuffer();

private:

	/** Stable sparse array of indices into PreAllocatedEasingResults for each easing channel */
	TSparseArray<int32> EasingChannelToIndex;

	/** Unstable array of preallocated storage for computing easing results sorted by hierarchical depth. */
	TArray<UE::MovieScene::FHierarchicalEasingChannelData> PreAllocatedComputationData;

	/** True if the preallocated easing results need resorting. */
	bool bResultsNeedResort;
};

