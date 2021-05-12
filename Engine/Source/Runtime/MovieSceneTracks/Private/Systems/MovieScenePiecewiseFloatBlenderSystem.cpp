// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieScenePiecewiseFloatBlenderSystem.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/EntityAllocationIterator.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"

#include "EntitySystem/MovieSceneEvalTimeSystem.h"
#include "Systems/FloatChannelEvaluatorSystem.h"
#include "Systems/MovieSceneQuaternionInterpolationRotationSystem.h"
#include "Systems/WeightAndEasingEvaluatorSystem.h"
#include "Algo/Find.h"
#include "Algo/AnyOf.h"
#include "Algo/Accumulate.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Sections/MovieSceneFloatSection.h"


DECLARE_CYCLE_STAT(TEXT("Piecewise Blender System"),             MovieSceneEval_PiecewiseBlenderSystem,  STATGROUP_MovieSceneECS);
DECLARE_CYCLE_STAT(TEXT("Blend float values"),                   MovieSceneEval_BlendFloatValues,        STATGROUP_MovieSceneECS);
DECLARE_CYCLE_STAT(TEXT("Default combine blended float values"), MovieSceneEval_BlendCombineFloatValues, STATGROUP_MovieSceneECS);
 
namespace UE
{
namespace MovieScene
{


struct FAccumulationResult
{
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

/** Task for accumulating all weighted blend inputs into arrays based on BlendID. Will be run for Absolute, Additive and Relative blend modes*/
struct FAccumulationTask
{
	FAccumulationTask(TSortedMap<FComponentTypeID, TArray<FBlendResult>>* InAccumulationBuffers)
		: AccumulationBuffers(InAccumulationBuffers)
	{}

	/** Task entry point - iterates the allocation's headers and accumulates float results for any required components */
	void ForEachAllocation(FEntityAllocationIteratorItem InItem, TRead<FMovieSceneBlendChannelID> BlendIDs, TReadOptional<float> OptionalEasingAndWeights)
	{
		const FEntityAllocation* Allocation = InItem;
		const FComponentMask& AllocationType = InItem;

		for (const FComponentHeader& ComponentHeader : Allocation->GetComponentHeaders())
		{
			if (TArray<FBlendResult>* AccumulationBuffer = AccumulationBuffers->Find(ComponentHeader.ComponentType))
			{
				ComponentHeader.ReadWriteLock.ReadLock();

				const float* FloatResults = static_cast<const float*>(ComponentHeader.GetValuePtr(0));
				AccumulateResults(Allocation, FloatResults, BlendIDs, OptionalEasingAndWeights, *AccumulationBuffer);

				ComponentHeader.ReadWriteLock.ReadUnlock();
			}
		}
	}

private:

	void AccumulateResults(const FEntityAllocation* InAllocation, const float* InFloatResults, const FMovieSceneBlendChannelID* BlendIDs, const float* OptionalEasingAndWeights, TArray<FBlendResult>& OutBlendResults)
	{
		static const FMovieSceneBlenderSystemID FloatBlenderSystemID = UMovieSceneBlenderSystem::GetBlenderSystemID<UMovieScenePiecewiseFloatBlenderSystem>();

		const int32 Num = InAllocation->Num();
		if (OptionalEasingAndWeights)
		{
			// We have some easing/weight factors to multiply values with.
			for (int32 Index = 0; Index < Num; ++Index)
			{
				const FMovieSceneBlendChannelID& BlendID(BlendIDs[Index]);
				ensureMsgf(BlendID.SystemID == FloatBlenderSystemID, TEXT("Overriding the standard blender system of standard types isn't supported."));

				FBlendResult& Result = OutBlendResults[BlendID.ChannelID];

				const float Weight = OptionalEasingAndWeights[Index];
				Result.Total  += InFloatResults[Index] * Weight;
				Result.Weight += Weight;
			}
		}
		else
		{
			// Faster path for when there's no weight to multiply values with.
			for (int32 Index = 0; Index < Num; ++Index)
			{
				const FMovieSceneBlendChannelID& BlendID(BlendIDs[Index]);
				ensureMsgf(BlendID.SystemID == FloatBlenderSystemID, TEXT("Overriding the standard blender system of standard types isn't supported."));

				FBlendResult& Result = OutBlendResults[BlendID.ChannelID];
				Result.Total  += InFloatResults[Index];
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
struct FAdditiveFromBaseBlendTask
{
	TSortedMap<FComponentTypeID, FAdditiveFromBaseBuffer>* AccumulationBuffers;

	void ForEachAllocation(FEntityAllocationIteratorItem InItem, TRead<FMovieSceneBlendChannelID> BlendIDs, TReadOptional<float> EasingAndWeightResults)
	{
		FEntityAllocation* Allocation = InItem;
		const FComponentMask& AllocationType = InItem;

		for (const FComponentHeader& ComponentHeader : Allocation->GetComponentHeaders())
		{
			if (FAdditiveFromBaseBuffer* Buffer = AccumulationBuffers->Find(ComponentHeader.ComponentType))
			{
				TComponentReader<float> BaseValues = Allocation->ReadComponents(Buffer->BaseComponent.ReinterpretCast<float>());
				TComponentReader<float> FloatResults(&ComponentHeader);

				AccumulateResults(Allocation, FloatResults.AsPtr(), BaseValues.AsPtr(), BlendIDs, EasingAndWeightResults, Buffer->Buffer);
			}
		}
	}

private:

	void AccumulateResults(const FEntityAllocation* InAllocation, const float* FloatResults, const float* BaseValues, const FMovieSceneBlendChannelID* BlendIDs, const float* OptionalEasingAndWeights, TArray<FBlendResult>& OutBlendResults)
	{
		static const FMovieSceneBlenderSystemID FloatBlenderSystemID = UMovieSceneBlenderSystem::GetBlenderSystemID<UMovieScenePiecewiseFloatBlenderSystem>();
		
		const int32 Num = InAllocation->Num();

		if (OptionalEasingAndWeights)
		{
			for (int32 Index = 0; Index < Num; ++Index)
			{
				const FMovieSceneBlendChannelID& BlendID(BlendIDs[Index]);
				ensureMsgf(BlendID.SystemID == FloatBlenderSystemID, TEXT("Overriding the standard blender system of standard types isn't supported."));

				FBlendResult& Result = OutBlendResults[BlendID.ChannelID];

				const float Weight = OptionalEasingAndWeights[Index];
				Result.Total  += (FloatResults[Index] - BaseValues[Index]) * Weight;
				Result.Weight += Weight;
			}
		}
		else
		{
			// Faster path for when there's no weight to multiply values with.
			for (int32 Index = 0; Index < Num; ++Index)
			{
				const FMovieSceneBlendChannelID& BlendID(BlendIDs[Index]);
				ensureMsgf(BlendID.SystemID == FloatBlenderSystemID, TEXT("Overriding the standard blender system of standard types isn't supported."));

				FBlendResult& Result = OutBlendResults[BlendID.ChannelID];
				Result.Total  += (FloatResults[Index] - BaseValues[Index]);
				Result.Weight += 1.f;
			}
		}
	}
};

/** Task that combines all accumulated blends for any tracked property type that has blend inputs/outputs */
struct FCombineBlends
{
	explicit FCombineBlends(const TBitArray<>& InCachedRelevantProperties, const FAccumulationBuffers* InAccumulationBuffers, FEntityAllocationWriteContext InWriteContext)
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
		static const FMovieSceneBlenderSystemID FloatBlenderSystemID = UMovieSceneBlenderSystem::GetBlenderSystemID<UMovieScenePiecewiseFloatBlenderSystem>();

		TArrayView<const FPropertyCompositeDefinition> Composites = PropertyRegistry->GetComposites(PropertyDefinition);

		FOptionalComponentReader OptInitialValues = Allocation->TryReadComponentsErased(PropertyDefinition.InitialValueType);

		for (int32 CompositeIndex = 0; CompositeIndex < Composites.Num(); ++CompositeIndex)
		{
			if ( (PropertyDefinition.FloatCompositeMask & (1 << CompositeIndex)) == 0)
			{
				continue;
			}

			TComponentTypeID<float> ResultComponent = Composites[CompositeIndex].ComponentTypeID.ReinterpretCast<float>();
			if (!AllocationType.Contains(ResultComponent))
			{
				continue;
			}

			FAccumulationResult Results = AccumulationBuffers->FindResults(ResultComponent);
			if (Results.IsValid())
			{
				const uint16 InitialValueProjectionOffset = Composites[CompositeIndex].CompositeOffset;

				// Open the float result channel for write
				TComponentWriter<float> FloatResults = Allocation->WriteComponents(ResultComponent, WriteContext);

				if (OptInitialValues)
				{
					for (int32 Index = 0; Index < Allocation->Num(); ++Index)
					{
						ensureMsgf(BlendIDs[Index].SystemID == FloatBlenderSystemID, TEXT("Overriding the standard blender system of standard types isn't supported."));
						const float InitialValue = *reinterpret_cast<const float*>(static_cast<const uint8*>(OptInitialValues[Index]) + InitialValueProjectionOffset);
						BlendResultsWithInitial(Results, BlendIDs[Index].ChannelID, InitialValue, FloatResults[Index]);
					}
				}
				else
				{
					for (int32 Index = 0; Index < Allocation->Num(); ++Index)
					{
						ensureMsgf(BlendIDs[Index].SystemID == FloatBlenderSystemID, TEXT("Overriding the standard blender system of standard types isn't supported."));
						BlendResults(Results, BlendIDs[Index].ChannelID, FloatResults[Index]);
					}
				}
			}
		}
	}

	void BlendResultsWithInitial(const FAccumulationResult& Results, uint16 BlendID, const float InitialValue, float& OutFinalBlendResult)
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
			const float AbsoluteBlendedValue = bInitialValueContributes ?
				(InitialValue * (1.f - AbsoluteResult.Weight) + AbsoluteResult.Total) :
				AbsoluteResult.Total;
			const float FinalTotalWeight = bInitialValueContributes ? (TotalWeight + (1.f - AbsoluteResult.Weight)) : TotalWeight;

			const float Value = (AbsoluteBlendedValue + RelativeResult.Total) / FinalTotalWeight + TotalAdditiveResult.Total;
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

	void BlendResults(const FAccumulationResult& Results, uint16 BlendID, float& OutFinalBlendResult)
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
			const float Value = AbsoluteResult.Total / AbsoluteResult.Weight + AdditiveResult.Total + AdditiveFromBaseResult.Total;
			OutFinalBlendResult = Value;
		}
	}

private:

	TBitArray<> CachedRelevantProperties;
	const FAccumulationBuffers* AccumulationBuffers;
	const FPropertyRegistry* PropertyRegistry;
	FEntityAllocationWriteContext WriteContext;
};



bool FAccumulationBuffers::IsEmpty() const
{
	return Absolute.Num() == 0 && Relative.Num() == 0 && Additive.Num() == 0 && AdditiveFromBase.Num() == 0;
}

void FAccumulationBuffers::Reset()
{
	Absolute.Empty();
	Relative.Empty();
	Additive.Empty();
	AdditiveFromBase.Empty();
}

FAccumulationResult FAccumulationBuffers::FindResults(FComponentTypeID InComponentType) const
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


} // namespace MovieScene
} // namespace UE

UMovieScenePiecewiseFloatBlenderSystem::UMovieScenePiecewiseFloatBlenderSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineImplicitPrerequisite(UFloatChannelEvaluatorSystem::StaticClass(), GetClass());
		DefineImplicitPrerequisite(UMovieSceneQuaternionInterpolationRotationSystem::StaticClass(), GetClass());
		DefineImplicitPrerequisite(UWeightAndEasingEvaluatorSystem::StaticClass(), GetClass());
	}
}

void UMovieScenePiecewiseFloatBlenderSystem::OnLink()
{
}

void UMovieScenePiecewiseFloatBlenderSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	SCOPE_CYCLE_COUNTER(MovieSceneEval_PiecewiseBlenderSystem)

	CompactBlendChannels();

	// We allocate space for every blend even if there are gaps so we can do a straight index into each array
	const int32 MaximumNumBlends = AllocatedBlendChannels.Num();
	if (MaximumNumBlends == 0)
	{
		return;
	}

	// Update cached channel data if necessary
	if (ChannelRelevancyCache.Update(Linker->EntityManager) == ECachedEntityManagerState::Stale)
	{
		ReinitializeAccumulationBuffers();
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
		.SetStat(GET_STATID(MovieSceneEval_BlendFloatValues))
		.Dispatch_PerAllocation<FAccumulationTask>(&Linker->EntityManager, InPrerequisites, nullptr, &AccumulationBuffers.Absolute);

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
		.SetStat(GET_STATID(MovieSceneEval_BlendFloatValues))
		.Dispatch_PerAllocation<FAccumulationTask>(&Linker->EntityManager, InPrerequisites, nullptr, &AccumulationBuffers.Relative);

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
		.SetStat(GET_STATID(MovieSceneEval_BlendFloatValues))
		.Dispatch_PerAllocation<FAccumulationTask>(&Linker->EntityManager, InPrerequisites, nullptr, &AccumulationBuffers.Additive);

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
		.SetStat(GET_STATID(MovieSceneEval_BlendFloatValues))
		.Dispatch_PerAllocation<FAdditiveFromBaseBlendTask>(&Linker->EntityManager, InPrerequisites, nullptr, &AccumulationBuffers.AdditiveFromBase);

		if (Task)
		{
			Prereqs.AddMasterTask(Task);
		}
	}

	// Master task that performs the actual blends
	FEntityTaskBuilder()
	.Read(BuiltInComponents->BlendChannelOutput)
	.FilterAny(BlendedPropertyMask)
	.SetStat(GET_STATID(MovieSceneEval_BlendCombineFloatValues))
	.Dispatch_PerAllocation<FCombineBlends>(&Linker->EntityManager, Prereqs, &Subsequents, CachedRelevantProperties, &AccumulationBuffers, FEntityAllocationWriteContext(Linker->EntityManager));
}

void UMovieScenePiecewiseFloatBlenderSystem::ReinitializeAccumulationBuffers()
{
	using namespace UE::MovieScene;

	const int32 MaximumNumBlends = AllocatedBlendChannels.Num();

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

	BlendedResultMask.Reset();
	AccumulationBuffers.Reset();

	// Recompute which result types are blended
	const int32 NumFloats = UE_ARRAY_COUNT(BuiltInComponents->FloatResult);
	for (int32 Index = 0; Index < NumFloats; ++Index)
	{
		TComponentTypeID<float> Component = BuiltInComponents->FloatResult[Index];

		const bool bHasAbsolutes         = Linker->EntityManager.Contains(FEntityComponentFilter().All({ Component, BuiltInComponents->BlendChannelInput, BuiltInComponents->Tags.AbsoluteBlend }));
		const bool bHasRelatives         = Linker->EntityManager.Contains(FEntityComponentFilter().All({ Component, BuiltInComponents->BlendChannelInput, BuiltInComponents->Tags.RelativeBlend }));
		const bool bHasAdditives         = Linker->EntityManager.Contains(FEntityComponentFilter().All({ Component, BuiltInComponents->BlendChannelInput, BuiltInComponents->Tags.AdditiveBlend }));
		const bool bHasAdditivesFromBase = Linker->EntityManager.Contains(FEntityComponentFilter().All({ Component, BuiltInComponents->BlendChannelInput, BuiltInComponents->Tags.AdditiveFromBaseBlend }));

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
			Buffer.BaseComponent = BuiltInComponents->BaseFloat[Index];
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
		if (PropertyDefinition.FloatCompositeMask != 0)
		{
			InclusionFilter.Reset();
			InclusionFilter.All({ BuiltInComponents->BlendChannelOutput, PropertyDefinition.PropertyType });
			if (Linker->EntityManager.Contains(InclusionFilter))
			{
				CachedRelevantProperties.PadToNum(PropertyTypeIndex + 1, false);
				CachedRelevantProperties[PropertyTypeIndex] = true;

				BlendedPropertyMask.Set(PropertyDefinition.PropertyType);
			}
		}
	}
}

void UMovieScenePiecewiseFloatBlenderSystem::ZeroAccumulationBuffers()
{
	using namespace UE::MovieScene;

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


FGraphEventRef UMovieScenePiecewiseFloatBlenderSystem::DispatchDecomposeTask(const UE::MovieScene::FFloatDecompositionParams& Params, UE::MovieScene::FAlignedDecomposedFloat* Output)
{
	using namespace UE::MovieScene;

	if (!Params.ResultComponentType)
	{
		return nullptr;
	}

	struct FChannelResultTask
	{
		TArray<FMovieSceneEntityID, TInlineAllocator<8>> EntitiesToDecompose;
		FAlignedDecomposedFloat* Result;
		uint16 DecomposeBlendChannel;
		FComponentTypeID AdditiveBlendTag;

		explicit FChannelResultTask(const FFloatDecompositionParams& Params, FAlignedDecomposedFloat* InResult)
			: Result(InResult)
			, DecomposeBlendChannel(Params.DecomposeBlendChannel)
			, AdditiveBlendTag(FBuiltInComponentTypes::Get()->Tags.AdditiveBlend)
		{
			EntitiesToDecompose.Append(Params.Query.Entities.GetData(), Params.Query.Entities.Num());
		}

		void ForEachAllocation(const FEntityAllocation* Allocation, TRead<FMovieSceneEntityID> EntityToDecomposeIDs, TRead<FMovieSceneBlendChannelID> BlendChannels, TRead<float> FloatResultComponent, TReadOptional<float> OptionalWeightComponent)
		{
			static const FMovieSceneBlenderSystemID FloatBlenderSystemID = UMovieSceneBlenderSystem::GetBlenderSystemID<UMovieScenePiecewiseFloatBlenderSystem>();

			const bool bAdditive = Allocation->HasComponent(AdditiveBlendTag);

			const int32 Num = Allocation->Num();
			for (int32 EntityIndex = 0; EntityIndex < Num; ++EntityIndex)
			{
				const FMovieSceneBlendChannelID& BlendChannel(BlendChannels[EntityIndex]);
				ensureMsgf(BlendChannel.SystemID == FloatBlenderSystemID, TEXT("Overriding the standard blender system of standard types isn't supported."));

				if (BlendChannel.ChannelID != DecomposeBlendChannel)
				{
					continue;
				}

				// We've found a contributor for this blend channel
				const FMovieSceneEntityID EntityToDecompose = EntityToDecomposeIDs[EntityIndex];
				const float               Weight            = OptionalWeightComponent ? OptionalWeightComponent[EntityIndex] : 1.f;
				const float               FloatResult       = FloatResultComponent[EntityIndex];

				if (EntitiesToDecompose.Contains(EntityToDecompose))
				{
					if (bAdditive)
					{
						Result->Value.DecomposedAdditives.Add(MakeTuple(EntityToDecompose, FWeightedFloat{ FloatResult, Weight }));
					}
					else
					{
						Result->Value.DecomposedAbsolutes.Add(MakeTuple(EntityToDecompose, FWeightedFloat{ FloatResult, Weight }));
					}
				}
				else if (bAdditive)
				{
					Result->Value.Result.Additive += FloatResult * Weight;
				}
				else
				{
					Result->Value.Result.Absolute.Value  += FloatResult * Weight;
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
			.Read(Params.ResultComponentType)
			.ReadOptional(BuiltInComponents->WeightAndEasingResult)
			.FilterAll({ Params.PropertyTag })
			.Dispatch_PerAllocation<FChannelResultTask>(&Linker->EntityManager, FSystemTaskPrerequisites(), nullptr, Params, Output);
	}
	else
	{
		return FEntityTaskBuilder()
			.ReadEntityIDs()
			.Read(BuiltInComponents->BlendChannelInput)
			.Read(Params.ResultComponentType)
			.ReadOptional(BuiltInComponents->WeightAndEasingResult)
			.FilterAll({ Params.PropertyTag })
			.Dispatch_PerAllocation<FChannelResultTask>(&Linker->EntityManager, FSystemTaskPrerequisites(), nullptr, Params, Output);
	}
}
