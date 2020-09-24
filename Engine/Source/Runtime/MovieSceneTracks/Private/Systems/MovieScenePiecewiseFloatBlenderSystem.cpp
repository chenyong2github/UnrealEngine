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

/** Task for generating blended and weighted results on blend outputs */
struct FBlendTask
{
	TArray<FBlendResult>* ResultArray;

	void ForEachAllocation(const FEntityAllocation* InAllocation, TRead<uint16> BlendID, TRead<float> FloatResult, TReadOptional<float> EasingAndWeightResult)
	{
		const int32 Num = InAllocation->Num();

		const uint16* BlendIDs      = BlendID.Resolve(InAllocation);
		const float*  FloatResults  = FloatResult.Resolve(InAllocation);

		// This is random access into the Blendables array
		if (InAllocation->HasComponent(EasingAndWeightResult.ComponentType))
		{
			// We have some easing/weight factors to multiply values with.
			const float* EasingAndWeights = EasingAndWeightResult.Resolve(InAllocation);

			for (int32 Index = 0; Index < Num; ++Index)
			{
				FBlendResult& Result = (*ResultArray)[BlendIDs[Index]];

				const float Weight = EasingAndWeights[Index];
				Result.Total  += FloatResults[Index] * Weight;
				Result.Weight += Weight;
			}
		}
		else
		{
			// Faster path for when there's no weight to multiply values with.
			for (int32 Index = 0; Index < Num; ++Index)
			{
				FBlendResult& Result = (*ResultArray)[BlendIDs[Index]];
				Result.Total  += FloatResults[Index];
				Result.Weight += 1.f;
			}
		}
	}
};

/** Same as the task above, but also reads a "base value" that is subtracted from all values.
 *
 *  Only used by entities with the "additive from base" blend type.
 */
struct FAdditiveFromBaseBlendTask
{
	TArray<FBlendResult>* ResultArray;

	void ForEachAllocation(const FEntityAllocation* InAllocation, TRead<uint16> BlendID, TRead<float> BaseValue, TRead<float> FloatResult, TReadOptional<float> EasingAndWeightResult)
	{
		const int32 Num = InAllocation->Num();

		const uint16* BlendIDs      = BlendID.Resolve(InAllocation);
		const float*  BaseValues    = BaseValue.Resolve(InAllocation);
		const float*  FloatResults  = FloatResult.Resolve(InAllocation);

		// This is random access into the Blendables array
		if (InAllocation->HasComponent(EasingAndWeightResult.ComponentType))
		{
			// We have some easing/weight factors to multiply values with.
			const float* EasingAndWeights = EasingAndWeightResult.Resolve(InAllocation);

			for (int32 Index = 0; Index < Num; ++Index)
			{
				FBlendResult& Result = (*ResultArray)[BlendIDs[Index]];

				const float Weight = EasingAndWeights[Index];
				Result.Total  += (FloatResults[Index] - BaseValues[Index]) * Weight;
				Result.Weight += Weight;
			}
		}
		else
		{
			// Faster path for when there's no weight to multiply values with.
			for (int32 Index = 0; Index < Num; ++Index)
			{
				FBlendResult& Result = (*ResultArray)[BlendIDs[Index]];
				Result.Total  += (FloatResults[Index] - BaseValues[Index]);
				Result.Weight += 1.f;
			}
		}
	}
};

struct FCombineBlendsWithInitialValues
{
	const FBlendedValuesTaskData* TaskData;
	int32 InitialValueProjectionOffset;

	explicit FCombineBlendsWithInitialValues(const FBlendedValuesTaskData* InTaskData, int32 InInitialValueProjectionOffset)
		: TaskData(InTaskData), InitialValueProjectionOffset(InInitialValueProjectionOffset)
	{
		check(InInitialValueProjectionOffset >= 0);
	}

	void ForEachEntity(uint16 BlendID, const void* ErasedInitialValue, float& OutFinalBlendResult)
	{
		checkSlow(InitialValueProjectionOffset != INDEX_NONE);

		const float InitialValue = *reinterpret_cast<const float*>(reinterpret_cast<const uint8*>(ErasedInitialValue) + InitialValueProjectionOffset);

		FBlendResult AbsoluteResult = TaskData->GetAbsoluteResult(BlendID);
		FBlendResult RelativeResult = TaskData->GetRelativeResult(BlendID);
		FBlendResult AdditiveResult = TaskData->GetAdditiveResult(BlendID);
		FBlendResult AdditiveFromBaseResult = TaskData->GetAdditiveFromBaseResult(BlendID);

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
};

struct FCombineBlends
{
	const FBlendedValuesTaskData* TaskData;

	explicit FCombineBlends(const FBlendedValuesTaskData* InTaskData)
		: TaskData(InTaskData)
	{}

	void ForEachEntity(uint16 BlendID, float& OutFinalBlendResult)
	{
		FBlendResult AbsoluteResult = TaskData->GetAbsoluteResult(BlendID);
		FBlendResult AdditiveResult = TaskData->GetAdditiveResult(BlendID);

#if DO_GUARD_SLOW
		ensureMsgf(AbsoluteResult.Weight != 0.f, TEXT("Default blend combine being used for an entity that has no absolute weight. This should have an initial value and should be handled by each system, and excluded by default with UMovieSceneBlenderSystem::FinalCombineExclusionFilter ."));
#endif

		const float TotalWeight = AbsoluteResult.Weight;
		if (TotalWeight != 0)
		{
			const float Value = AbsoluteResult.Total / AbsoluteResult.Weight + AdditiveResult.Total;
			OutFinalBlendResult = Value;
		}
	}
};

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
	using namespace UE::MovieScene;
	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

	const size_t NumFloats = UE_ARRAY_COUNT(BuiltInComponents->FloatResult);
	for (size_t Index = 0; Index < NumFloats; ++Index)
	{
		FChannelData& NewData = ChannelData.Emplace_GetRef();
		NewData.ResultComponent = BuiltInComponents->FloatResult[Index];
		NewData.BaseValueComponent = BuiltInComponents->BaseFloat[Index];
	}
}

void UMovieScenePiecewiseFloatBlenderSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	SCOPE_CYCLE_COUNTER(MovieSceneEval_PiecewiseBlenderSystem)

	// @todo: scheduled routine maintenance like this to optimize memory layouts
	const int32 LastBlendIndex = AllocatedBlendChannels.FindLast(true);
	if (LastBlendIndex == INDEX_NONE)
	{
		AllocatedBlendChannels.Empty();
	}
	else if (LastBlendIndex < AllocatedBlendChannels.Num() - 1)
	{
		AllocatedBlendChannels.RemoveAt(LastBlendIndex + 1, AllocatedBlendChannels.Num() - LastBlendIndex - 1);
	}

	// We allocate space for every blend even if there are gaps so we can do a straight index into each array
	const int32 MaximumNumBlends = AllocatedBlendChannels.Num();
	if (MaximumNumBlends == 0)
	{
		return;
	}

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

	// Update cached channel data if necessary
	if (ChannelRelevancyCache.Update(Linker->EntityManager) == ECachedEntityManagerState::Stale)
	{
		// Update channel relevancy
		for (FChannelData& Channel : ChannelData)
		{
			Channel.bEnabled = Linker->EntityManager.Contains(FEntityComponentFilter().All({ Channel.ResultComponent, BuiltInComponents->BlendChannelOutput }));
			if (!Channel.bEnabled)
			{
				Channel.bHasAbsolutes = false;
				Channel.bHasRelatives = false;
				Channel.bHasAdditives = false;
				Channel.bHasAdditivesFromBase = false;
			}
			else
			{
				Channel.bHasAbsolutes = Linker->EntityManager.Contains(FEntityComponentFilter().All({ Channel.ResultComponent, BuiltInComponents->BlendChannelInput, BuiltInComponents->Tags.AbsoluteBlend }));
				Channel.bHasRelatives = Linker->EntityManager.Contains(FEntityComponentFilter().All({ Channel.ResultComponent, BuiltInComponents->BlendChannelInput, BuiltInComponents->Tags.RelativeBlend }));
				Channel.bHasAdditives = Linker->EntityManager.Contains(FEntityComponentFilter().All({ Channel.ResultComponent, BuiltInComponents->BlendChannelInput, BuiltInComponents->Tags.AdditiveBlend }));
				Channel.bHasAdditivesFromBase = Linker->EntityManager.Contains(FEntityComponentFilter().All({ Channel.ResultComponent, BuiltInComponents->BlendChannelInput, BuiltInComponents->Tags.AdditiveFromBaseBlend }));
			}
		}

		// Update property relevancy
		CachedRelevantProperties.Empty();

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
					CachedRelevantProperties.Add(PropertyTypeIndex);
				}
			}
		}
	}


	FGraphEventArray SingleBlendTasks;
	for (FChannelData& Channel : ChannelData)
	{
		if (!Channel.bEnabled)
		{
			continue;
		}

		SingleBlendTasks.Reset();

		FTaskDataSchedule& TaskData = TaskDataByType.FindOrAdd(Channel.ResultComponent);

		if (!TaskData.Impl)
		{
			TaskData.Impl = MakeUnique<FBlendedValuesTaskData>(Channel.ResultComponent);
		}

		checkf(TaskData.Impl->bTasksComplete, TEXT("Attempting to issue blend tasks while some are still pending - this is a threading policy violation"));

		TaskData.Prerequisite = nullptr;

		if (Channel.bHasAbsolutes)
		{
			if (!TaskData.Impl->Absolutes)
			{
				TaskData.Impl->Absolutes.Emplace();
			}
			TaskData.Impl->Absolutes->SetNum(MaximumNumBlends);
			FMemory::Memzero(TaskData.Impl->Absolutes.GetValue().GetData(), sizeof(FBlendResult)*MaximumNumBlends);

			// Run a task that blends all absolutes into the Absolutes array
			FGraphEventRef AbsolutesTask = FEntityTaskBuilder()

				// Blend ID
				.Read(BuiltInComponents->BlendChannelInput)

				// Evaluated float result
				.Read(Channel.ResultComponent)

				// Optional easing result component
				.ReadOptional(BuiltInComponents->WeightAndEasingResult)

				// Only include absolute blends and active entities
				.FilterAll({ BuiltInComponents->Tags.AbsoluteBlend })

				.FilterNone({ BuiltInComponents->Tags.Ignored })

				.SetStat(GET_STATID(MovieSceneEval_BlendFloatValues))

				// Dispatch the task
				.Dispatch_PerAllocation<FBlendTask>(&Linker->EntityManager, InPrerequisites, nullptr, &TaskData.Impl->Absolutes.GetValue());

			if (AbsolutesTask)
			{
				SingleBlendTasks.Add(AbsolutesTask);
			}
		}
		else
		{
			TaskData.Impl->Absolutes.Reset();
		}

		if (Channel.bHasRelatives)
		{
			if (!TaskData.Impl->Relatives)
			{
				TaskData.Impl->Relatives.Emplace();
			}
			TaskData.Impl->Relatives->SetNum(MaximumNumBlends);
			FMemory::Memzero(TaskData.Impl->Relatives.GetValue().GetData(), sizeof(FBlendResult)*MaximumNumBlends);

			// Run a task that blends all absolutes into the Relatives array
			FGraphEventRef RelativesTask = FEntityTaskBuilder()

				// Blend ID
				.Read(BuiltInComponents->BlendChannelInput)

				// Evaluated float result
				.Read(Channel.ResultComponent)

				// Optional easing result component
				.ReadOptional(BuiltInComponents->WeightAndEasingResult)

				// Only include relative blends and active entities
				.FilterAll({ BuiltInComponents->Tags.RelativeBlend })

				.FilterNone({ BuiltInComponents->Tags.Ignored })

				.SetStat(GET_STATID(MovieSceneEval_BlendFloatValues))

				// Dispatch the task
				.Dispatch_PerAllocation<FBlendTask>(&Linker->EntityManager, InPrerequisites, nullptr, &TaskData.Impl->Relatives.GetValue());

			if (RelativesTask)
			{
				SingleBlendTasks.Add(RelativesTask);
			}
		}
		else
		{
			TaskData.Impl->Relatives.Reset();
		}

		if (Channel.bHasAdditives)
		{
			if (!TaskData.Impl->Additives)
			{
				TaskData.Impl->Additives.Emplace();
			}
			TaskData.Impl->Additives->SetNum(MaximumNumBlends);
			FMemory::Memzero(TaskData.Impl->Additives.GetValue().GetData(), sizeof(FBlendResult)*MaximumNumBlends);

			// Run a task that blends all absolutes into the Additives array
			FGraphEventRef AdditivesTask = FEntityTaskBuilder()

				// Blend ID
				.Read(BuiltInComponents->BlendChannelInput)

				// Evaluated float result
				.Read(Channel.ResultComponent)

				// Optional easing result component
				.ReadOptional(BuiltInComponents->WeightAndEasingResult)

				// Only include additive blends and active entities
				.FilterAll({ BuiltInComponents->Tags.AdditiveBlend })

				.FilterNone({ BuiltInComponents->Tags.Ignored })

				.SetStat(GET_STATID(MovieSceneEval_BlendFloatValues))

				// Dispatch the task
				.Dispatch_PerAllocation<FBlendTask>(&Linker->EntityManager, InPrerequisites, nullptr, &TaskData.Impl->Additives.GetValue());

			if (AdditivesTask)
			{
				SingleBlendTasks.Add(AdditivesTask);
			}
		}
		else
		{
			TaskData.Impl->Additives.Reset();
		}

		if (Channel.bHasAdditivesFromBase)
		{
			if (!TaskData.Impl->AdditivesFromBase)
			{
				TaskData.Impl->AdditivesFromBase.Emplace();
			}
			TaskData.Impl->AdditivesFromBase->SetNum(MaximumNumBlends);
			FMemory::Memzero(TaskData.Impl->AdditivesFromBase.GetValue().GetData(), sizeof(FBlendResult)*MaximumNumBlends);

			// Run a task that blends all absolutes into the AdditivesFromBase array
			//
			// This is a slightly different task than the other 3 tasks because it reads the base value from the entities
			//
			FGraphEventRef AdditivesFromBaseTask = FEntityTaskBuilder()

				// Blend ID
				.Read(BuiltInComponents->BlendChannelInput)

				// Base value
				.Read(Channel.BaseValueComponent)

				// Evaluated float result
				.Read(Channel.ResultComponent)

				// Optional easing result component
				.ReadOptional(BuiltInComponents->WeightAndEasingResult)

				// Only include additive blends and active entities
				.FilterAll({ BuiltInComponents->Tags.AdditiveFromBaseBlend })

				.FilterNone({ BuiltInComponents->Tags.Ignored })

				.SetStat(GET_STATID(MovieSceneEval_BlendFloatValues))

				// Dispatch the task
				.Dispatch_PerAllocation<FAdditiveFromBaseBlendTask>(&Linker->EntityManager, InPrerequisites, nullptr, &TaskData.Impl->AdditivesFromBase.GetValue());

			if (AdditivesFromBaseTask)
			{
				SingleBlendTasks.Add(AdditivesFromBaseTask);
			}
		}
		else
		{
			TaskData.Impl->AdditivesFromBase.Reset();
		}

		if (SingleBlendTasks.Num() > 0)
		{
			auto OnBlendsComplete = [TaskData = TaskData.Impl.Get()]
			{
				TaskData->bTasksComplete = true;
			};

			TaskData.Prerequisite = TGraphTask<TFunctionGraphTaskImpl<void(), ESubsequentsMode::TrackSubsequents>>::CreateTask(&SingleBlendTasks, Linker->EntityManager.GetDispatchThread())
				.ConstructAndDispatchWhenReady(MoveTemp(OnBlendsComplete), TStatId(), ENamedThreads::AnyHiPriThreadHiPriTask);
		}
		else
		{
			TaskData.Impl->bTasksComplete = true;
			TaskData.Prerequisite = nullptr;
		}
	}

	FComponentMask InitialValueMask;

	TArrayView<const FPropertyDefinition> Properties = BuiltInComponents->PropertyRegistry.GetProperties();
	for (int32 PropertyTypeIndex : CachedRelevantProperties)
	{
		const FPropertyDefinition& PropertyDefinition = Properties[PropertyTypeIndex];
		check(PropertyDefinition.FloatCompositeMask != 0);

		InitialValueMask.Set(PropertyDefinition.InitialValueType);

		// Blend anything with an initial value for this property type
		TArrayView<const FPropertyCompositeDefinition> Composites = BuiltInComponents->PropertyRegistry.GetComposites(PropertyDefinition);
		for (int32 CompositeIndex = 0; CompositeIndex < Composites.Num(); ++CompositeIndex)
		{
			if ( (PropertyDefinition.FloatCompositeMask & (1 << CompositeIndex)) == 0)
			{
				continue;
			}

			TComponentTypeID<float> ResultComponent = Composites[CompositeIndex].ComponentTypeID.ReinterpretCast<float>();
			const FTaskDataSchedule* TaskData = RetrieveTaskData(ResultComponent);
			if (!TaskData)
			{
				continue;
			}

			FSystemTaskPrerequisites Prereqs { TaskData->Prerequisite };

			// Master task that performs the actual blends
			FEntityTaskBuilder()
			.Read(BuiltInComponents->BlendChannelOutput)
			.ReadErased(PropertyDefinition.InitialValueType)
			.Write(ResultComponent)
			.SetStat(GET_STATID(MovieSceneEval_BlendCombineFloatValues))
			.Dispatch_PerEntity<FCombineBlendsWithInitialValues>(&Linker->EntityManager, Prereqs, &Subsequents, TaskData->GetData(), Composites[CompositeIndex].CompositeOffset);
		}
	}

	// Default blend tasks for anything that doesn't have initial values
	for (FChannelData& Channel : ChannelData)
	{
		if (!Channel.bEnabled)
		{
			continue;
		}

		const FTaskDataSchedule* TaskData = RetrieveTaskData(Channel.ResultComponent);
		if (TaskData)
		{
			FSystemTaskPrerequisites Prereqs { TaskData->Prerequisite };

			FEntityTaskBuilder()
			.Read(BuiltInComponents->BlendChannelOutput)
			.Write(Channel.ResultComponent)
			.FilterNone(InitialValueMask)
			.SetStat(GET_STATID(MovieSceneEval_BlendCombineFloatValues))
			.Dispatch_PerEntity<FCombineBlends>(&Linker->EntityManager, Prereqs, &Subsequents, TaskData->GetData());
		}
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

		void ForEachAllocation(const FEntityAllocation* Allocation, FReadEntityIDs EntityToDecomposeIDComponent, TRead<uint16> BlendChannelComponent, TRead<float> FloatResultComponent, TReadOptional<float> OptionalWeightComponent)
		{
			TArrayView<const FMovieSceneEntityID> EntityToDecomposeIDs = EntityToDecomposeIDComponent.ResolveAsArray(Allocation);
			ForEachAllocationImpl(Allocation, EntityToDecomposeIDs, BlendChannelComponent, FloatResultComponent, OptionalWeightComponent);
		}

		void ForEachAllocation(const FEntityAllocation* Allocation, TRead<FMovieSceneEntityID> EntityToDecomposeIDComponent, TRead<uint16> BlendChannelComponent, TRead<float> FloatResultComponent, TReadOptional<float> OptionalWeightComponent)
		{
			TArrayView<const FMovieSceneEntityID> EntityToDecomposeIDs = EntityToDecomposeIDComponent.ResolveAsArray(Allocation);
			ForEachAllocationImpl(Allocation, EntityToDecomposeIDs, BlendChannelComponent, FloatResultComponent, OptionalWeightComponent);
		}

		void ForEachAllocationImpl(const FEntityAllocation* Allocation, TArrayView<const FMovieSceneEntityID> EntityToDecomposeIDs, TRead<uint16> BlendChannelComponent, TRead<float> FloatResultComponent, TReadOptional<float> OptionalWeightComponent)
		{
			const bool bAdditive = Allocation->HasComponent(AdditiveBlendTag);

			TArrayView<const uint16> BlendChannels = BlendChannelComponent.ResolveAsArray(Allocation);
			for (int32 EntityIndex = 0; EntityIndex < BlendChannels.Num(); ++EntityIndex)
			{
				if (BlendChannels[EntityIndex] != DecomposeBlendChannel)
				{
					continue;
				}

				// We've found a contributor for this blend channel
				const FMovieSceneEntityID EntityToDecompose = EntityToDecomposeIDs[EntityIndex];
				TArrayView<const float>   Weights           = OptionalWeightComponent.ResolveAsArray(Allocation);
				const float               Weight            = (Weights.Num() != 0 ? Weights[EntityIndex] : 1.f);
				const float               FloatResult       = FloatResultComponent.ResolveAsArray(Allocation)[EntityIndex];

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
