// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/SortedMap.h"
#include "Containers/Set.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieSceneEntityInstantiatorSystem.h"
#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/EntityAllocationIterator.h"

#include "EntitySystem/MovieSceneSequenceInstanceHandle.h"
#include "WeightAndEasingEvaluatorSystem.generated.h"

class UMovieSceneEvalTimeSystem;
struct FMovieSceneSubSequenceData;

namespace UE
{
namespace MovieScene
{

	struct FHierarchicalEasingChannelContributorData
	{
		UE::MovieScene::FInstanceHandle SubSequenceHandle;
		float EasingResult;
	};

	struct FHierarchicalEasingChannelData
	{
		TArray<FHierarchicalEasingChannelContributorData, TInlineAllocator<8>> Contributors;
		float FinalEasingResult;
	};

}  // namespace MovieScene
}  // namespace UE

/**
 * System that creates hierarchical easing channels
 */
UCLASS()
class MOVIESCENETRACKS_API UMovieSceneHierarchicalEasingInstantiatorSystem : public UMovieSceneEntityInstantiatorSystem
{
public:

	GENERATED_BODY()

	UMovieSceneHierarchicalEasingInstantiatorSystem(const FObjectInitializer& ObjInit);

private:

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;

private:

	/** Map between a sub-sequence handle and the easing channel affecting it */
	TMap<UE::MovieScene::FInstanceHandle, uint16> InstanceHandleToEasingChannel;
};

/**
 * System that is responsible for evaluating ease in/out factors.
 */
UCLASS()
class MOVIESCENETRACKS_API UWeightAndEasingEvaluatorSystem : public UMovieSceneEntitySystem
{
public:

	GENERATED_BODY()

	UWeightAndEasingEvaluatorSystem(const FObjectInitializer& ObjInit);

	uint16 AllocateEasingChannel(UE::MovieScene::FInstanceHandle SubSequenceHandle);
	void ReleaseEasingChannel(uint16 EasingChannelID);

	void SetSubSequenceEasing(UE::MovieScene::FInstanceHandle SubSequenceHandle, float EasingResult);

private:

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;
	virtual bool IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const override;

private:

	TSparseArray<UE::MovieScene::FHierarchicalEasingChannelData> EasingChannels;
};

