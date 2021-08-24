// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieScenePiecewiseBlenderSystemImpl.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/EntityAllocationIterator.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneEntityManager.h"
#include "Systems/MovieScenePiecewiseDoubleBlenderSystem.h"
#include "Systems/MovieScenePiecewiseFloatBlenderSystem.h"
#include "Systems/WeightAndEasingEvaluatorSystem.h"

namespace UE
{
namespace MovieScene
{

/** Traits for known blendable values (float and double) */
template<typename ValueType>
struct TPiecewiseBlendableValueTraits;

template<>
struct TPiecewiseBlendableValueTraits<float>
{
	static const FMovieSceneBlenderSystemID GetBlenderSystemID()
	{
		return UMovieSceneBlenderSystem::GetBlenderSystemID<UMovieScenePiecewiseFloatBlenderSystem>();
	}
	static bool HasAnyComposite(const FPropertyDefinition& PropertyDefinition)
	{
		return PropertyDefinition.FloatCompositeMask != 0;
	}
	static bool IsCompositeSupported(const FPropertyDefinition& PropertyDefinition, int32 CompositeIndex)
	{
		return ((PropertyDefinition.FloatCompositeMask & (1 << CompositeIndex)) != 0);
	}
	static TArrayView<TComponentTypeID<float>> GetBaseComponents()
	{
		return TArrayView<TComponentTypeID<float>>(FBuiltInComponentTypes::Get()->BaseFloat);
	}
	static TArrayView<TComponentTypeID<float>> GetResultComponents()
	{
		return TArrayView<TComponentTypeID<float>>(FBuiltInComponentTypes::Get()->FloatResult);
	}
};

template<>
struct TPiecewiseBlendableValueTraits<double>
{
	static const FMovieSceneBlenderSystemID GetBlenderSystemID()
	{
		return UMovieSceneBlenderSystem::GetBlenderSystemID<UMovieScenePiecewiseDoubleBlenderSystem>();
	}
	static bool HasAnyComposite(const FPropertyDefinition& PropertyDefinition)
	{
		return PropertyDefinition.DoubleCompositeMask != 0;
	}
	static bool IsCompositeSupported(const FPropertyDefinition& PropertyDefinition, int32 CompositeIndex)
	{
		return ((PropertyDefinition.DoubleCompositeMask & (1 << CompositeIndex)) != 0);
	}
	static TArrayView<TComponentTypeID<double>> GetBaseComponents()
	{
		return TArrayView<TComponentTypeID<double>>(FBuiltInComponentTypes::Get()->BaseDouble);
	}
	static TArrayView<TComponentTypeID<double>> GetResultComponents()
	{
		return TArrayView<TComponentTypeID<double>>(FBuiltInComponentTypes::Get()->DoubleResult);
	}
};

/** Task for accumulating all weighted blend inputs into arrays based on BlendID. Will be run for Absolute, Additive and Relative blend modes*/
template<typename ValueType>
struct TAccumulationTask
{
	using FBlendResult = TBlendResult<ValueType>;

	TAccumulationTask(TSortedMap<FComponentTypeID, TArray<FBlendResult>>* InAccumulationBuffers)
		: AccumulationBuffers(InAccumulationBuffers)
	{}

	/** Task entry point - iterates the allocation's headers and accumulates results for any required components */
	void ForEachAllocation(FEntityAllocationIteratorItem InItem, TRead<FMovieSceneBlendChannelID> BlendIDs, TReadOptional<float> OptionalEasingAndWeights)
	{
		const FEntityAllocation* Allocation = InItem;
		const FComponentMask& AllocationType = InItem;

		for (const FComponentHeader& ComponentHeader : Allocation->GetComponentHeaders())
		{
			if (TArray<FBlendResult>* AccumulationBuffer = AccumulationBuffers->Find(ComponentHeader.ComponentType))
			{
				ComponentHeader.ReadWriteLock.ReadLock();

				const ValueType* Results = static_cast<const ValueType*>(ComponentHeader.GetValuePtr(0));
				AccumulateResults(Allocation, Results, BlendIDs, OptionalEasingAndWeights, *AccumulationBuffer);

				ComponentHeader.ReadWriteLock.ReadUnlock();
			}
		}
	}

private:

	void AccumulateResults(const FEntityAllocation* InAllocation, const ValueType* InResults, const FMovieSceneBlendChannelID* BlendIDs, const float* OptionalEasingAndWeights, TArray<FBlendResult>& OutBlendResults)
	{
		static const FMovieSceneBlenderSystemID BlenderSystemID = TPiecewiseBlendableValueTraits<ValueType>::GetBlenderSystemID();

		const int32 Num = InAllocation->Num();
		if (OptionalEasingAndWeights)
		{
			// We have some easing/weight factors to multiply values with.
			for (int32 Index = 0; Index < Num; ++Index)
			{
				const FMovieSceneBlendChannelID& BlendID(BlendIDs[Index]);
				ensureMsgf(BlendID.SystemID == BlenderSystemID, TEXT("Overriding the standard blender system of standard types isn't supported."));

				FBlendResult& Result = OutBlendResults[BlendID.ChannelID];

				const float Weight = OptionalEasingAndWeights[Index];
				Result.Total  += InResults[Index] * Weight;
				Result.Weight += Weight;
			}
		}
		else
		{
			// Faster path for when there's no weight to multiply values with.
			for (int32 Index = 0; Index < Num; ++Index)
			{
				const FMovieSceneBlendChannelID& BlendID(BlendIDs[Index]);
				ensureMsgf(BlendID.SystemID == BlenderSystemID, TEXT("Overriding the standard blender system of standard types isn't supported."));

				FBlendResult& Result = OutBlendResults[BlendID.ChannelID];
				Result.Total  += InResults[Index];
				Result.Weight += 1.f;
			}
		}
	}

	TSortedMap<FComponentTypeID, TArray<FBlendResult>>* AccumulationBuffers;
};

/** Same as the task above, but also reads a "base value" that is subtracted from all values.
 *
 *  Only used by entities with the "additive from base" blend type.
 */
template<typename ValueType>
struct TAdditiveFromBaseBlendTask
{
	using FAdditiveFromBaseBuffer = TAdditiveFromBaseBuffer<ValueType>;
	using FBlendResult = TBlendResult<ValueType>;

	TSortedMap<FComponentTypeID, FAdditiveFromBaseBuffer>* AccumulationBuffers;

	void ForEachAllocation(FEntityAllocationIteratorItem InItem, TRead<FMovieSceneBlendChannelID> BlendIDs, TReadOptional<float> EasingAndWeightResults)
	{
		FEntityAllocation* Allocation = InItem;
		const FComponentMask& AllocationType = InItem;

		for (const FComponentHeader& ComponentHeader : Allocation->GetComponentHeaders())
		{
			if (FAdditiveFromBaseBuffer* Buffer = AccumulationBuffers->Find(ComponentHeader.ComponentType))
			{
				TComponentReader<ValueType> BaseValues = Allocation->ReadComponents(Buffer->BaseComponent.template ReinterpretCast<ValueType>());
				TComponentReader<ValueType> Results(&ComponentHeader);

				AccumulateResults(Allocation, Results.AsPtr(), BaseValues.AsPtr(), BlendIDs, EasingAndWeightResults, Buffer->Buffer);
			}
		}
	}

private:

	void AccumulateResults(const FEntityAllocation* InAllocation, const ValueType* Results, const ValueType* BaseValues, const FMovieSceneBlendChannelID* BlendIDs, const float* OptionalEasingAndWeights, TArray<FBlendResult>& OutBlendResults)
	{
		static const FMovieSceneBlenderSystemID BlenderSystemID = TPiecewiseBlendableValueTraits<ValueType>::GetBlenderSystemID();

		const int32 Num = InAllocation->Num();

		if (OptionalEasingAndWeights)
		{
			for (int32 Index = 0; Index < Num; ++Index)
			{
				const FMovieSceneBlendChannelID& BlendID(BlendIDs[Index]);
				ensureMsgf(BlendID.SystemID == BlenderSystemID, TEXT("Overriding the standard blender system of standard types isn't supported."));

				FBlendResult& Result = OutBlendResults[BlendID.ChannelID];

				const float Weight = OptionalEasingAndWeights[Index];
				Result.Total  += (Results[Index] - BaseValues[Index]) * Weight;
				Result.Weight += Weight;
			}
		}
		else
		{
			// Faster path for when there's no weight to multiply values with.
			for (int32 Index = 0; Index < Num; ++Index)
			{
				const FMovieSceneBlendChannelID& BlendID(BlendIDs[Index]);
				ensureMsgf(BlendID.SystemID == BlenderSystemID, TEXT("Overriding the standard blender system of standard types isn't supported."));

				FBlendResult& Result = OutBlendResults[BlendID.ChannelID];
				Result.Total  += (Results[Index] - BaseValues[Index]);
				Result.Weight += 1.f;
			}
		}
	}
};

/** Task that combines all accumulated blends for any tracked property type that has blend inputs/outputs */
template<typename ValueType>
struct TCombineBlends
{
	using FBlendResult = TBlendResult<ValueType>;
	using FAccumulationResult = TAccumulationResult<ValueType>;
	using FAccumulationBuffers = TAccumulationBuffers<ValueType>;
	using FPiecewiseBlendableValueTraits = TPiecewiseBlendableValueTraits<ValueType>;

	explicit TCombineBlends(const TBitArray<>& InCachedRelevantProperties, const FAccumulationBuffers* InAccumulationBuffers, FEntityAllocationWriteContext InWriteContext)
		: CachedRelevantProperties(InCachedRelevantProperties)
		, AccumulationBuffers(InAccumulationBuffers)
		, PropertyRegistry(&FBuiltInComponentTypes::Get()->PropertyRegistry)
		, WriteContext(InWriteContext)
	{}

	void ForEachAllocation(FEntityAllocationIteratorItem InItem, TRead<FMovieSceneBlendChannelID> BlendIDs)
	{
		FEntityAllocation* Allocation = InItem;
		const FComponentMask& AllocationType = InItem;

		// Find out what kind of property this is
		for (TConstSetBitIterator<> PropertyIndex(CachedRelevantProperties); PropertyIndex; ++PropertyIndex)
		{
			const FPropertyDefinition& PropertyDefinition = PropertyRegistry->GetDefinition(FCompositePropertyTypeID::FromIndex(PropertyIndex.GetIndex()));
			if (AllocationType.Contains(PropertyDefinition.PropertyType))
			{
				ProcessPropertyType(Allocation, AllocationType, PropertyDefinition, BlendIDs);
				return;
			}
		}
	}

private:

	void ProcessPropertyType(FEntityAllocation* Allocation, const FComponentMask& AllocationType, const FPropertyDefinition& PropertyDefinition, const FMovieSceneBlendChannelID* BlendIDs)
	{
		static const FMovieSceneBlenderSystemID BlenderSystemID = TPiecewiseBlendableValueTraits<ValueType>::GetBlenderSystemID();

		TArrayView<const FPropertyCompositeDefinition> Composites = PropertyRegistry->GetComposites(PropertyDefinition);

		FOptionalComponentReader OptInitialValues = Allocation->TryReadComponentsErased(PropertyDefinition.InitialValueType);

		for (int32 CompositeIndex = 0; CompositeIndex < Composites.Num(); ++CompositeIndex)
		{
			if (!FPiecewiseBlendableValueTraits::IsCompositeSupported(PropertyDefinition, CompositeIndex))
			{
				continue;
			}

			TComponentTypeID<ValueType> ResultComponent = Composites[CompositeIndex].ComponentTypeID.template ReinterpretCast<ValueType>();
			if (!AllocationType.Contains(ResultComponent))
			{
				continue;
			}

			FAccumulationResult Results = AccumulationBuffers->FindResults(ResultComponent);
			if (Results.IsValid())
			{
				const uint16 InitialValueProjectionOffset = Composites[CompositeIndex].CompositeOffset;

				// Open the result channel for write
				TComponentWriter<ValueType> ValueResults = Allocation->WriteComponents(ResultComponent, WriteContext);

				if (OptInitialValues)
				{
					for (int32 Index = 0; Index < Allocation->Num(); ++Index)
					{
						ensureMsgf(BlendIDs[Index].SystemID == BlenderSystemID, TEXT("Overriding the standard blender system of standard types isn't supported."));
						const ValueType InitialValue = *reinterpret_cast<const ValueType*>(static_cast<const uint8*>(OptInitialValues[Index]) + InitialValueProjectionOffset);
						BlendResultsWithInitial(Results, BlendIDs[Index].ChannelID, InitialValue, ValueResults[Index]);
					}
				}
				else
				{
					for (int32 Index = 0; Index < Allocation->Num(); ++Index)
					{
						ensureMsgf(BlendIDs[Index].SystemID == BlenderSystemID, TEXT("Overriding the standard blender system of standard types isn't supported."));
						BlendResults(Results, BlendIDs[Index].ChannelID, ValueResults[Index]);
					}
				}
			}
		}
	}

	void BlendResultsWithInitial(const FAccumulationResult& Results, uint16 BlendID, const ValueType InitialValue, ValueType& OutFinalBlendResult)
	{
		FBlendResult AbsoluteResult = Results.GetAbsoluteResult(BlendID);
		FBlendResult RelativeResult = Results.GetRelativeResult(BlendID);
		FBlendResult AdditiveResult = Results.GetAdditiveResult(BlendID);
		FBlendResult AdditiveFromBaseResult = Results.GetAdditiveFromBaseResult(BlendID);

		if (RelativeResult.Weight != 0)
		{
			RelativeResult.Total += InitialValue * RelativeResult.Weight;
		}

		FBlendResult TotalAdditiveResult = { AdditiveResult.Total + AdditiveFromBaseResult.Total, AdditiveResult.Weight + AdditiveFromBaseResult.Weight };

		const float TotalWeight = AbsoluteResult.Weight + RelativeResult.Weight;
		if (TotalWeight != 0)
		{
			// If the absolute value has some partial weighting (for ease-in/out for instance), we ramp it from/to the initial value. This means
			// that the "initial value" adds a contribution to the entire blending process, so we add its weight to the total that we
			// normalize absolutes and relatives with.
			//
			// Note that "partial weighting" means strictly between 0 and 100%. At 100% and above, we don't need to do this thing with the initial
			// value. At 0%, we have no absolute value (only a relative value) and we therefore don't want to include the initial value either.
			const bool bInitialValueContributes = (0.f < AbsoluteResult.Weight && AbsoluteResult.Weight < 1.f);
			const ValueType AbsoluteBlendedValue = bInitialValueContributes ?
				(InitialValue * (1.f - AbsoluteResult.Weight) + AbsoluteResult.Total) :
				AbsoluteResult.Total;
			const float FinalTotalWeight = bInitialValueContributes ? (TotalWeight + (1.f - AbsoluteResult.Weight)) : TotalWeight;

			const ValueType Value = (AbsoluteBlendedValue + RelativeResult.Total) / FinalTotalWeight + TotalAdditiveResult.Total;
			OutFinalBlendResult = Value;
		}
		else if (TotalAdditiveResult.Weight != 0)
		{
			OutFinalBlendResult = TotalAdditiveResult.Total + InitialValue;
		}
		else
		{
			OutFinalBlendResult = InitialValue;
		}
	}

	void BlendResults(const FAccumulationResult& Results, uint16 BlendID, ValueType& OutFinalBlendResult)
	{
		FBlendResult AbsoluteResult = Results.GetAbsoluteResult(BlendID);
		FBlendResult AdditiveResult = Results.GetAdditiveResult(BlendID);
		FBlendResult AdditiveFromBaseResult = Results.GetAdditiveFromBaseResult(BlendID);

#if DO_GUARD_SLOW
		ensureMsgf(AbsoluteResult.Weight != 0.f, TEXT("Default blend combine being used for an entity that has no absolute weight. This should have an initial value and should be handled by each system, and excluded by default with UMovieSceneBlenderSystem::FinalCombineExclusionFilter ."));
#endif

		const float TotalWeight = AbsoluteResult.Weight;
		if (TotalWeight != 0)
		{
			const ValueType Value = AbsoluteResult.Total / AbsoluteResult.Weight + AdditiveResult.Total + AdditiveFromBaseResult.Total;
			OutFinalBlendResult = Value;
		}
	}

private:

	TBitArray<> CachedRelevantProperties;
	const FAccumulationBuffers* AccumulationBuffers;
	const FPropertyRegistry* PropertyRegistry;
	FEntityAllocationWriteContext WriteContext;
};

template<typename ValueType>
bool TAccumulationBuffers<ValueType>::IsEmpty() const
{
	return Absolute.Num() == 0 && Relative.Num() == 0 && Additive.Num() == 0 && AdditiveFromBase.Num() == 0;
}

template<typename ValueType>
void TAccumulationBuffers<ValueType>::Reset()
{
	Absolute.Empty();
	Relative.Empty();
	Additive.Empty();
	AdditiveFromBase.Empty();
}

template<typename ValueType>
typename TAccumulationBuffers<ValueType>::FAccumulationResult TAccumulationBuffers<ValueType>::FindResults(FComponentTypeID InComponentType) const
{
	FAccumulationResult Result;
	if (const TArray<FBlendResult>* Absolutes = Absolute.Find(InComponentType))
	{
		Result.Absolutes = Absolutes->GetData();
	}
	if (const TArray<FBlendResult>* Relatives = Relative.Find(InComponentType))
	{
		Result.Relatives = Relatives->GetData();
	}
	if (const TArray<FBlendResult>* Additives = Additive.Find(InComponentType))
	{
		Result.Additives = Additives->GetData();
	}
	if (const FAdditiveFromBaseBuffer* AdditivesFromBase = AdditiveFromBase.Find(InComponentType))
	{
		Result.AdditivesFromBase = AdditivesFromBase->Buffer.GetData();
	}
	return Result;
}

template<typename ValueType>
void TPiecewiseBlenderSystemImpl<ValueType>::Run(FPiecewiseBlenderSystemImplRunParams& Params, FEntityManager& EntityManager, FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using FAccumulationTask = TAccumulationTask<ValueType>;
	using FAdditiveFromBaseBlendTask = TAdditiveFromBaseBlendTask<ValueType>;
	using FCombineBlends = TCombineBlends<ValueType>;

	// We allocate space for every blend even if there are gaps so we can do a straight index into each array
	if (Params.MaximumNumBlends == 0)
	{
		return;
	}

	// Update cached channel data if necessary
	if (ChannelRelevancyCache.Update(EntityManager) == ECachedEntityManagerState::Stale)
	{
		ReinitializeAccumulationBuffers(Params.MaximumNumBlends, EntityManager);
	}

	if (AccumulationBuffers.IsEmpty())
	{
		return;
	}

	ZeroAccumulationBuffers();

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

	FSystemTaskPrerequisites Prereqs;
	if (AccumulationBuffers.Absolute.Num() != 0)
	{
		FGraphEventRef Task = FEntityTaskBuilder()
		.Read(BuiltInComponents->BlendChannelInput)
		.ReadOptional(BuiltInComponents->WeightAndEasingResult)
		.FilterAll({ BuiltInComponents->Tags.AbsoluteBlend })
		.FilterAny(BlendedResultMask)
		.FilterNone({ BuiltInComponents->Tags.Ignored })
		.SetStat(Params.BlendValuesStatId)
		.template Dispatch_PerAllocation<FAccumulationTask>(&EntityManager, InPrerequisites, nullptr, &AccumulationBuffers.Absolute);

		if (Task)
		{
			Prereqs.AddMasterTask(Task);
		}
	}

	if (AccumulationBuffers.Relative.Num() != 0)
	{
		FGraphEventRef Task = FEntityTaskBuilder()
		.Read(BuiltInComponents->BlendChannelInput)
		.ReadOptional(BuiltInComponents->WeightAndEasingResult)
		.FilterAll({ BuiltInComponents->Tags.RelativeBlend })
		.FilterAny(BlendedResultMask)
		.FilterNone({ BuiltInComponents->Tags.Ignored })
		.SetStat(Params.BlendValuesStatId)
		.template Dispatch_PerAllocation<FAccumulationTask>(&EntityManager, InPrerequisites, nullptr, &AccumulationBuffers.Relative);

		if (Task)
		{
			Prereqs.AddMasterTask(Task);
		}
	}

	if (AccumulationBuffers.Additive.Num() != 0)
	{
		FGraphEventRef Task = FEntityTaskBuilder()
		.Read(BuiltInComponents->BlendChannelInput)
		.ReadOptional(BuiltInComponents->WeightAndEasingResult)
		.FilterAll({ BuiltInComponents->Tags.AdditiveBlend })
		.FilterAny(BlendedResultMask)
		.FilterNone({ BuiltInComponents->Tags.Ignored })
		.SetStat(Params.BlendValuesStatId)
		.template Dispatch_PerAllocation<FAccumulationTask>(&EntityManager, InPrerequisites, nullptr, &AccumulationBuffers.Additive);

		if (Task)
		{
			Prereqs.AddMasterTask(Task);
		}
	}

	if (AccumulationBuffers.AdditiveFromBase.Num() != 0)
	{
		FGraphEventRef Task = FEntityTaskBuilder()
		.Read(BuiltInComponents->BlendChannelInput)
		.ReadOptional(BuiltInComponents->WeightAndEasingResult)
		.FilterAll({ BuiltInComponents->Tags.AdditiveFromBaseBlend })
		.FilterAny(BlendedResultMask)
		.FilterNone({ BuiltInComponents->Tags.Ignored })
		.SetStat(Params.BlendValuesStatId)
		.template Dispatch_PerAllocation<FAdditiveFromBaseBlendTask>(&EntityManager, InPrerequisites, nullptr, &AccumulationBuffers.AdditiveFromBase);

		if (Task)
		{
			Prereqs.AddMasterTask(Task);
		}
	}

	// Master task that performs the actual blends
	FEntityTaskBuilder()
	.Read(BuiltInComponents->BlendChannelOutput)
	.FilterAny(BlendedPropertyMask)
	.SetStat(Params.CombineBlendsStatId)
	.template Dispatch_PerAllocation<FCombineBlends>(&EntityManager, Prereqs, &Subsequents, CachedRelevantProperties, &AccumulationBuffers, FEntityAllocationWriteContext(EntityManager));
}

template<typename ValueType>
void TPiecewiseBlenderSystemImpl<ValueType>::ReinitializeAccumulationBuffers(int32 MaximumNumBlends, FEntityManager& EntityManager)
{
	using FPiecewiseBlendableValueTraits = TPiecewiseBlendableValueTraits<ValueType>;

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

	BlendedResultMask.Reset();
	AccumulationBuffers.Reset();

	TArrayView<TComponentTypeID<ValueType>> BaseComponents = FPiecewiseBlendableValueTraits::GetBaseComponents();
	TArrayView<TComponentTypeID<ValueType>> ResultComponents = FPiecewiseBlendableValueTraits::GetResultComponents();
	check(BaseComponents.Num() == ResultComponents.Num());

	// Recompute which result types are blended
	const int32 NumResults = ResultComponents.Num();
	for (int32 Index = 0; Index < NumResults; ++Index)
	{
		TComponentTypeID<ValueType> Component = ResultComponents[Index];

		const bool bHasAbsolutes         = EntityManager.Contains(FEntityComponentFilter().All({ Component, BuiltInComponents->BlendChannelInput, BuiltInComponents->Tags.AbsoluteBlend }));
		const bool bHasRelatives         = EntityManager.Contains(FEntityComponentFilter().All({ Component, BuiltInComponents->BlendChannelInput, BuiltInComponents->Tags.RelativeBlend }));
		const bool bHasAdditives         = EntityManager.Contains(FEntityComponentFilter().All({ Component, BuiltInComponents->BlendChannelInput, BuiltInComponents->Tags.AdditiveBlend }));
		const bool bHasAdditivesFromBase = EntityManager.Contains(FEntityComponentFilter().All({ Component, BuiltInComponents->BlendChannelInput, BuiltInComponents->Tags.AdditiveFromBaseBlend }));

		if (!(bHasAbsolutes || bHasRelatives || bHasAdditives || bHasAdditivesFromBase))
		{
			continue;
		}

		BlendedResultMask.Set(Component);

		if (bHasAbsolutes)
		{
			TArray<FBlendResult>& Buffer = AccumulationBuffers.Absolute.Add(Component);
			Buffer.SetNum(MaximumNumBlends);
		}
		if (bHasRelatives)
		{
			TArray<FBlendResult>& Buffer = AccumulationBuffers.Relative.Add(Component);
			Buffer.SetNum(MaximumNumBlends);
		}
		if (bHasAdditives)
		{
			TArray<FBlendResult>& Buffer = AccumulationBuffers.Additive.Add(Component);
			Buffer.SetNum(MaximumNumBlends);
		}
		if (bHasAdditivesFromBase)
		{
			FAdditiveFromBaseBuffer& Buffer = AccumulationBuffers.AdditiveFromBase.Add(Component);
			Buffer.Buffer.SetNum(MaximumNumBlends);
			Buffer.BaseComponent = BaseComponents[Index];
		}
	}

	// Update property relevancy
	CachedRelevantProperties.Empty();

	// If we have no accumulation buffers, we have nothing more to do
	if (AccumulationBuffers.IsEmpty())
	{
		return;
	}

	// This code works on the assumption that properties can never be removed (which is safe)
	FEntityComponentFilter InclusionFilter;
	TArrayView<const FPropertyDefinition> Properties = BuiltInComponents->PropertyRegistry.GetProperties();
	for (int32 PropertyTypeIndex = 0; PropertyTypeIndex < Properties.Num(); ++PropertyTypeIndex)
	{
		const FPropertyDefinition& PropertyDefinition = Properties[PropertyTypeIndex];
		if (FPiecewiseBlendableValueTraits::HasAnyComposite(PropertyDefinition))
		{
			InclusionFilter.Reset();
			InclusionFilter.All({ BuiltInComponents->BlendChannelOutput, PropertyDefinition.PropertyType });
			if (EntityManager.Contains(InclusionFilter))
			{
				CachedRelevantProperties.PadToNum(PropertyTypeIndex + 1, false);
				CachedRelevantProperties[PropertyTypeIndex] = true;

				BlendedPropertyMask.Set(PropertyDefinition.PropertyType);
			}
		}
	}
}

template<typename ValueType>
void TPiecewiseBlenderSystemImpl<ValueType>::ZeroAccumulationBuffers()
{
	// Arrays should only ever exist in these containers if they have size (they are always initialized to MaximumNumBlends in ReinitializeAccumulationBuffers)
	for (TPair<FComponentTypeID, TArray<FBlendResult>>& Pair : AccumulationBuffers.Absolute)
	{
		FMemory::Memzero(Pair.Value.GetData(), sizeof(FBlendResult)*Pair.Value.Num());
	}
	for (TPair<FComponentTypeID, TArray<FBlendResult>>& Pair : AccumulationBuffers.Relative)
	{
		FMemory::Memzero(Pair.Value.GetData(), sizeof(FBlendResult)*Pair.Value.Num());
	}
	for (TPair<FComponentTypeID, TArray<FBlendResult>>& Pair : AccumulationBuffers.Additive)
	{
		FMemory::Memzero(Pair.Value.GetData(), sizeof(FBlendResult)*Pair.Value.Num());
	}
	for (TPair<FComponentTypeID, FAdditiveFromBaseBuffer>& Pair : AccumulationBuffers.AdditiveFromBase)
	{
		FMemory::Memzero(Pair.Value.Buffer.GetData(), sizeof(FBlendResult)*Pair.Value.Buffer.Num());
	}
}

template<typename ValueType>
FGraphEventRef TPiecewiseBlenderSystemImpl<ValueType>::DispatchDecomposeTask(FEntityManager& EntityManager, const UE::MovieScene::FValueDecompositionParams& Params, UE::MovieScene::FAlignedDecomposedValue* Output)
{
	using namespace UE::MovieScene;

	if (!Params.ResultComponentType)
	{
		return nullptr;
	}

	TComponentTypeID<ValueType> ResultComponentType = Params.ResultComponentType.ReinterpretCast<ValueType>();

	struct FChannelResultTask
	{
		using FPiecewiseBlendableValueTraits = TPiecewiseBlendableValueTraits<ValueType>;

		TArray<FMovieSceneEntityID, TInlineAllocator<8>> EntitiesToDecompose;
		FAlignedDecomposedValue* Result;
		uint16 DecomposeBlendChannel;
		FComponentTypeID AdditiveBlendTag;

		explicit FChannelResultTask(const FValueDecompositionParams& Params, FAlignedDecomposedValue* InResult)
			: Result(InResult)
			, DecomposeBlendChannel(Params.DecomposeBlendChannel)
			, AdditiveBlendTag(FBuiltInComponentTypes::Get()->Tags.AdditiveBlend)
		{
			EntitiesToDecompose.Append(Params.Query.Entities.GetData(), Params.Query.Entities.Num());
		}

		void ForEachAllocation(const FEntityAllocation* Allocation, TRead<FMovieSceneEntityID> EntityToDecomposeIDs, TRead<FMovieSceneBlendChannelID> BlendChannels, TRead<ValueType> ValueResultComponent, TReadOptional<float> OptionalWeightComponent)
		{
			static const FMovieSceneBlenderSystemID BlenderSystemID = FPiecewiseBlendableValueTraits::GetBlenderSystemID();

			const bool bAdditive = Allocation->HasComponent(AdditiveBlendTag);

			const int32 Num = Allocation->Num();
			for (int32 EntityIndex = 0; EntityIndex < Num; ++EntityIndex)
			{
				const FMovieSceneBlendChannelID& BlendChannel(BlendChannels[EntityIndex]);
				ensureMsgf(BlendChannel.SystemID == BlenderSystemID, TEXT("Overriding the standard blender system of standard types isn't supported."));

				if (BlendChannel.ChannelID != DecomposeBlendChannel)
				{
					continue;
				}

				// We've found a contributor for this blend channel
				const FMovieSceneEntityID EntityToDecompose = EntityToDecomposeIDs[EntityIndex];
				const float               Weight            = OptionalWeightComponent ? OptionalWeightComponent[EntityIndex] : 1.f;
				const ValueType           ValueResult       = ValueResultComponent[EntityIndex];

				if (EntitiesToDecompose.Contains(EntityToDecompose))
				{
					if (bAdditive)
					{
						Result->Value.DecomposedAdditives.Add(MakeTuple(EntityToDecompose, FWeightedValue{ ValueResult, Weight }));
					}
					else
					{
						Result->Value.DecomposedAbsolutes.Add(MakeTuple(EntityToDecompose, FWeightedValue{ ValueResult, Weight }));
					}
				}
				else if (bAdditive)
				{
					Result->Value.Result.Additive += ValueResult * Weight;
				}
				else
				{
					Result->Value.Result.Absolute.Value  += ValueResult * Weight;
					Result->Value.Result.Absolute.Weight += Weight;
				}
			}
		}
	};

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

	if (Params.Query.bConvertFromSourceEntityIDs)
	{
		return FEntityTaskBuilder()
			.Read(BuiltInComponents->ParentEntity)
			.Read(BuiltInComponents->BlendChannelInput)
			.Read(ResultComponentType)
			.ReadOptional(BuiltInComponents->WeightAndEasingResult)
			.FilterAll({ Params.PropertyTag })
			.template Dispatch_PerAllocation<FChannelResultTask>(&EntityManager, FSystemTaskPrerequisites(), nullptr, Params, Output);
	}
	else
	{
		return FEntityTaskBuilder()
			.ReadEntityIDs()
			.Read(BuiltInComponents->BlendChannelInput)
			.Read(ResultComponentType)
			.ReadOptional(BuiltInComponents->WeightAndEasingResult)
			.FilterAll({ Params.PropertyTag })
			.template Dispatch_PerAllocation<FChannelResultTask>(&EntityManager, FSystemTaskPrerequisites(), nullptr, Params, Output);
	}
}

template struct MOVIESCENETRACKS_API TPiecewiseBlenderSystemImpl<float>;
template struct MOVIESCENETRACKS_API TPiecewiseBlenderSystemImpl<double>;

} // namespace MovieScene
} // namespace UE
