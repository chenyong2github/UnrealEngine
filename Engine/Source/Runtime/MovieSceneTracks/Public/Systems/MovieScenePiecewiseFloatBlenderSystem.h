// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/MovieSceneBlenderSystem.h"
#include "EntitySystem/MovieSceneDecompositionQuery.h"
#include "EntitySystem/MovieSceneCachedEntityFilterResult.h"
#include "Containers/SortedMap.h"

#include "MovieScenePiecewiseFloatBlenderSystem.generated.h"

namespace UE
{
namespace MovieScene
{
	struct FBlendResult
	{
		float Total = 0.f;
		float Weight = 0.f;
	};

	struct FBlendedValuesTaskData
	{
		FBlendedValuesTaskData(UE::MovieScene::TComponentTypeID<float> InResultComponent) 
			: ResultComponent(InResultComponent)
		{}

		FBlendResult GetAbsoluteResult(uint16 BlendID) const
		{
			checkf(bTasksComplete, TEXT("Attempting to access task data while tasks are still in progress - this is a threading policy violation. Clients must wait on FBlendedValuesTaskData::Prerequisites prior to accessing task data."));
			return Absolutes ? Absolutes.GetValue()[BlendID] : FBlendResult{};
		}
		FBlendResult GetRelativeResult(uint16 BlendID) const
		{
			checkf(bTasksComplete, TEXT("Attempting to access task data while tasks are still in progress - this is a threading policy violation. Clients must wait on FBlendedValuesTaskData::Prerequisites prior to accessing task data."));
			return Relatives ? Relatives.GetValue()[BlendID] : FBlendResult{};
		}
		FBlendResult GetAdditiveResult(uint16 BlendID) const
		{
			checkf(bTasksComplete, TEXT("Attempting to access task data while tasks are still in progress - this is a threading policy violation. Clients must wait on FBlendedValuesTaskData::Prerequisites prior to accessing task data."));
			return Additives ? Additives.GetValue()[BlendID] : FBlendResult{};
		}
		FBlendResult GetAdditiveFromBaseResult(uint16 BlendID) const
		{
			checkf(bTasksComplete, TEXT("Attempting to access task data while tasks are still in progress - this is a threading policy violation. Clients must wait on FBlendedValuesTaskData::Prerequisites prior to accessing task data."));
			return AdditivesFromBase ? AdditivesFromBase.GetValue()[BlendID] : FBlendResult{};
		}

	private:
		friend UMovieScenePiecewiseFloatBlenderSystem;

		UE::MovieScene::TComponentTypeID<float> ResultComponent;
		TOptional<TArray<FBlendResult>> Absolutes;
		TOptional<TArray<FBlendResult>> Relatives;
		TOptional<TArray<FBlendResult>> Additives;
		TOptional<TArray<FBlendResult>> AdditivesFromBase;
		bool bTasksComplete = true;
	};

	struct FTaskDataSchedule
	{
		FGraphEventRef GetPrerequisite() const
		{
			return Prerequisite;
		}

		const FBlendedValuesTaskData* GetData() const
		{
			return Impl.Get();
		}

	private:

		friend UMovieScenePiecewiseFloatBlenderSystem;

		// Heap allocated so that reallocation of the BlendResultsByType map doesn't move any of the arrays
		TUniquePtr<FBlendedValuesTaskData> Impl;

		FGraphEventRef Prerequisite;
	};
} // namespace MovieScene
} // namespace UE



UCLASS()
class MOVIESCENETRACKS_API UMovieScenePiecewiseFloatBlenderSystem : public UMovieSceneBlenderSystem, public IMovieSceneFloatDecomposer
{
public:
	GENERATED_BODY()

	UMovieScenePiecewiseFloatBlenderSystem(const FObjectInitializer& ObjInit);

	using FMovieSceneEntityID  = UE::MovieScene::FMovieSceneEntityID;
	using FComponentTypeID     = UE::MovieScene::FComponentTypeID;

	virtual void OnLink() override;
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;

	const UE::MovieScene::FTaskDataSchedule* RetrieveTaskData(TComponentTypeID<float> InComponentType) const
	{
		return TaskDataByType.Find(InComponentType);
	}

	virtual FGraphEventRef DispatchDecomposeTask(const UE::MovieScene::FFloatDecompositionParams& Params, UE::MovieScene::FAlignedDecomposedFloat* Output) override;

private:


	struct FChannelData
	{
		FChannelData()
		{
			bEnabled = false;
			bHasAbsolutes = false;
			bHasRelatives = false;
			bHasAdditives = false;
			bHasAdditivesFromBase = false;
		}

		TComponentTypeID<float> ResultComponent;
		TComponentTypeID<float> BaseValueComponent;

		bool bEnabled : 1;
		bool bHasAbsolutes : 1;
		bool bHasRelatives : 1;
		bool bHasAdditives : 1;
		bool bHasAdditivesFromBase : 1;
	};
	TArray<FChannelData, TInlineAllocator<10>> ChannelData;

	TMap<TComponentTypeID<float>, UE::MovieScene::FTaskDataSchedule> TaskDataByType;

	UE::MovieScene::FCachedEntityManagerState ChannelRelevancyCache;
	TArray<int32> CachedRelevantProperties;
};

