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
	struct FAccumulationResult;

	/** Blend result struct that stores the cumulative sum of pre-weighted values, alongside the total weight */
	struct FBlendResult
	{
		/** Cumulative sum of blend values pre-multiplied with each value's weight. */
		float Total = 0.f;
		/** Cumulative sum of weights. */
		float Weight = 0.f;
	};

	/** Buffer used for accumulating additive-from-base values */
	struct FAdditiveFromBaseBuffer
	{
		TArray<FBlendResult> Buffer;
		TComponentTypeID<float> BaseComponent;
	};

	/** Struct that maintains accumulation buffers for each blend type, one buffer per float result component type */
	struct FAccumulationBuffers
	{
		bool IsEmpty() const;

		void Reset();

		FAccumulationResult FindResults(FComponentTypeID InComponentType) const;

		/** Map from Float Result component type -> Absolute blend accumulation buffer for that channel type */
		TSortedMap<FComponentTypeID, TArray<FBlendResult>> Absolute;
		/** Map from Float Result component type -> Relative blend accumulation buffer for that channel type */
		TSortedMap<FComponentTypeID, TArray<FBlendResult>> Relative;
		/** Map from Float Result component type -> Additive blend accumulation buffer for that channel type */
		TSortedMap<FComponentTypeID, TArray<FBlendResult>> Additive;
		/** Map from Float Result component type -> Additive From Base blend accumulation buffer for that channel type */
		TSortedMap<FComponentTypeID, FAdditiveFromBaseBuffer> AdditiveFromBase;
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

	virtual FGraphEventRef DispatchDecomposeTask(const UE::MovieScene::FFloatDecompositionParams& Params, UE::MovieScene::FAlignedDecomposedFloat* Output) override;

private:

	void ReinitializeAccumulationBuffers();
	void ZeroAccumulationBuffers();

	/** Buffers that contain accumulated blend values, separated by blend type */
	UE::MovieScene::FAccumulationBuffers AccumulationBuffers;

	/** Mask that contains FloatResult components that have BlendChannelInput components */
	UE::MovieScene::FComponentMask BlendedResultMask;

	/** Mask that contains property tags for any property type that has has at least one BlendChannelOutput */
	UE::MovieScene::FComponentMask BlendedPropertyMask;

	/** Cache state that is used to invalidate and reset the accumulation buffers when the entity manager has structurally changed */
	UE::MovieScene::FCachedEntityManagerState ChannelRelevancyCache;

	/** Bit array specifying FCompositePropertyTypeID's for properties contained within BlendedPropertyMask */
	TBitArray<> CachedRelevantProperties;
};

