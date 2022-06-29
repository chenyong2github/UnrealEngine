// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/MovieSceneDecompositionQuery.h"
#include "EntitySystem/MovieSceneCachedEntityFilterResult.h"
#include "Containers/SortedMap.h"

namespace UE
{
namespace MovieScene
{

/** Blend result struct that stores the cumulative sum of pre-weighted values, alongside the total weight */
template<typename ValueType>
struct TBlendResult
{
	/** Cumulative sum of blend values pre-multiplied with each value's weight. */
	ValueType Total = 0.f;
	/** Cumulative sum of weights. */
	float Weight = 0.f;
};

/** Structure for holding the blend results of each blend type */
template<typename ValueType>
struct TAccumulationResult
{
	using FBlendResult = TBlendResult<ValueType>;

	const FBlendResult* Absolutes = nullptr;
	const FBlendResult* Relatives = nullptr;
	const FBlendResult* Additives = nullptr;
	const FBlendResult* AdditivesFromBase = nullptr;

	bool IsValid() const
	{
		return Absolutes || Relatives || Additives || AdditivesFromBase;
	}

	FBlendResult GetAbsoluteResult(uint16 BlendID) const
	{
		return Absolutes ? Absolutes[BlendID] : FBlendResult{};
	}
	FBlendResult GetRelativeResult(uint16 BlendID) const
	{
		return Relatives ? Relatives[BlendID] : FBlendResult{};
	}
	FBlendResult GetAdditiveResult(uint16 BlendID) const
	{
		return Additives ? Additives[BlendID] : FBlendResult{};
	}
	FBlendResult GetAdditiveFromBaseResult(uint16 BlendID) const
	{
		return AdditivesFromBase ? AdditivesFromBase[BlendID] : FBlendResult{};
	}
};

/** Buffer used for accumulating additive-from-base values */
template<typename ValueType>
struct TAdditiveFromBaseBuffer
{
	TArray<TBlendResult<ValueType>> Buffer;
	TComponentTypeID<ValueType> BaseComponent;
};

/** Struct that maintains accumulation buffers for each blend type, one buffer per float result component type */
template<typename ValueType>
struct TAccumulationBuffers
{
	using FBlendResult = TBlendResult<ValueType>;
	using FAccumulationResult = TAccumulationResult<ValueType>;
	using FAdditiveFromBaseBuffer = TAdditiveFromBaseBuffer<ValueType>;

	bool IsEmpty() const;

	void Reset();

	FAccumulationResult FindResults(FComponentTypeID InComponentType) const;

	/** Map from value result component type -> Absolute blend accumulation buffer for that channel type */
	TSortedMap<FComponentTypeID, TArray<FBlendResult>> Absolute;
	/** Map from value result component type -> Relative blend accumulation buffer for that channel type */
	TSortedMap<FComponentTypeID, TArray<FBlendResult>> Relative;
	/** Map from value result component type -> Additive blend accumulation buffer for that channel type */
	TSortedMap<FComponentTypeID, TArray<FBlendResult>> Additive;
	/** Map from value result component type -> Additive From Base blend accumulation buffer for that channel type */
	TSortedMap<FComponentTypeID, FAdditiveFromBaseBuffer> AdditiveFromBase;
};

/**
 * Parameters for running the piecewise blender.
 */
struct FPiecewiseBlenderSystemImplRunParams
{
	int32 MaximumNumBlends = 0;

	TStatId BlendValuesStatId;
	TStatId CombineBlendsStatId;
};

/**
 * Utility class for implementing piecewise blending given a floating precision type (float or double).
 */
template<typename ValueType>
struct TPiecewiseBlenderSystemImpl
{
public:

	/** Runs the blender system to blend all evaluated value channels of the given type */
	void Run(FPiecewiseBlenderSystemImplRunParams& Params, FEntityManager& EntityManager, FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents);

	/** Decomposes the given value */
	FGraphEventRef DispatchDecomposeTask(FEntityManager& EntityManager, const UE::MovieScene::FValueDecompositionParams& Params, UE::MovieScene::FAlignedDecomposedValue* Output);

private:

	using FBlendResult = TBlendResult<ValueType>;
	using FAccumulationBuffers = TAccumulationBuffers<ValueType>;
	using FAdditiveFromBaseBuffer = TAdditiveFromBaseBuffer<ValueType>;

	void ReinitializeAccumulationBuffers(int32 MaximumNumBlends, FEntityManager& EntityManager);
	void ZeroAccumulationBuffers();

	/** Buffers that contain accumulated blend values, separated by blend type */
	FAccumulationBuffers AccumulationBuffers;

	/** Mask that contains value result components that have BlendChannelInput components */
	FComponentMask BlendedResultMask;

	/** Mask that contains property tags for any property type that has has at least one BlendChannelOutput */
	FComponentMask BlendedPropertyMask;

	/** Cache state that is used to invalidate and reset the accumulation buffers when the entity manager has structurally changed */
	FCachedEntityManagerState ChannelRelevancyCache;

	/** Bit array specifying FCompositePropertyTypeID's for properties contained within BlendedPropertyMask */
	TBitArray<> CachedRelevantProperties;

	/** Whether the current entity manager contains any non-property based blends */
	bool bContainsNonPropertyBlends = false;
};

} // namespace MovieScene
} // namespace UE
