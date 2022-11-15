// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/WeightAndEasingEvaluatorSystem.h"
#include "Async/TaskGraphInterfaces.h"
#include "Compilation/MovieSceneCompiledDataManager.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/EntityAllocationIterator.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneEntityMutations.h"
#include "EntitySystem/MovieSceneRootInstantiatorSystem.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "Systems/MovieScenePropertyInstantiator.h"
#include "EntitySystem/Interrogation/MovieSceneInterrogatedPropertyInstantiator.h"
#include "Sections/MovieSceneSubSection.h"
#include "Systems/FloatChannelEvaluatorSystem.h"
#include "Systems/DoubleChannelEvaluatorSystem.h"
#include "IMovieScenePlayer.h"
#include "MovieSceneSection.h"

#include "EntitySystem/MovieSceneEvalTimeSystem.h"

#include "Algo/Find.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WeightAndEasingEvaluatorSystem)

DECLARE_CYCLE_STAT(TEXT("MovieScene: Evaluate easing"), MovieSceneEval_EvaluateEasingTask, STATGROUP_MovieSceneECS);
DECLARE_CYCLE_STAT(TEXT("MovieScene: Harvest easing"), MovieSceneEval_HarvestEasingTask, STATGROUP_MovieSceneECS);


namespace UE::MovieScene
{

static constexpr uint16 INVALID_EASING_CHANNEL = uint16(-1);

struct FAddEasingChannelToProviderMutation : IMovieSceneEntityMutation
{
	FAddEasingChannelToProviderMutation(UMovieSceneHierarchicalEasingInstantiatorSystem* InSystem)
		: System(InSystem)
		, BuiltInComponents(FBuiltInComponentTypes::Get())
		, InstanceRegistry(InSystem->GetLinker()->GetInstanceRegistry())
	{}

private:

	void CreateMutation(FEntityManager* EntityManager, FComponentMask* InOutEntityComponentTypes) const override
	{
		InOutEntityComponentTypes->Set(BuiltInComponents->HierarchicalEasingChannel);
	}

	void InitializeAllocation(FEntityAllocation* Allocation, const FComponentMask& AllocationType) const override
	{
		AllocateEasingChannelsForAllocation(Allocation);
	}

	void InitializeUnmodifiedAllocation(FEntityAllocation* Allocation, const FComponentMask& AllocationType) const override
	{
		AllocateEasingChannelsForAllocation(Allocation);
	}

	void AllocateEasingChannelsForAllocation(FEntityAllocation* Allocation) const
	{
		FEntityAllocationWriteContext NewAllocation = FEntityAllocationWriteContext::NewAllocation();

		TComponentLock<TWrite<uint16>>               EasingChannels      = Allocation->WriteComponents(BuiltInComponents->HierarchicalEasingChannel, NewAllocation);
		TComponentLock<TRead<FRootInstanceHandle>>   RootInstanceHandles = Allocation->ReadComponents(BuiltInComponents->RootInstanceHandle);
		TComponentLock<TRead<FMovieSceneSequenceID>> SubSequenceIDs      = Allocation->ReadComponents(BuiltInComponents->HierarchicalEasingProvider);
		
		for (int32 Index = 0; Index < Allocation->Num(); ++Index)
		{
			EasingChannels[Index] = System->AllocateEasingChannel(InstanceRegistry, RootInstanceHandles[Index], SubSequenceIDs[Index]).EasingChannelID;
		}
	}
private:

	UMovieSceneHierarchicalEasingInstantiatorSystem* System;
	FBuiltInComponentTypes* BuiltInComponents;
	FInstanceRegistry* InstanceRegistry;
};



struct FAddEasingChannelToConsumerMutation : IMovieSceneConditionalEntityMutation
{
	FEntityManager* EntityManager;
	FAddEasingChannelToConsumerMutation(UMovieSceneHierarchicalEasingInstantiatorSystem* InSystem)
		: System(InSystem)
		, BuiltInComponents(FBuiltInComponentTypes::Get())
		, InstanceRegistry(InSystem->GetLinker()->GetInstanceRegistry())
	{
		EntityManager = &InSystem->GetLinker()->EntityManager;
	}

private:

	void MarkAllocation(FEntityAllocation* Allocation, TBitArray<>& OutEntitiesToMutate) const override
	{
		TComponentReader<FRootInstanceHandle> RootInstanceHandles = Allocation->ReadComponents(BuiltInComponents->RootInstanceHandle);
		TOptionalComponentReader<FMovieSceneSequenceID> SubSequenceIDs = Allocation->TryReadComponents(BuiltInComponents->SequenceID);

		if (SubSequenceIDs)
		{
			for (int32 Index = 0; Index < Allocation->Num(); ++Index)
			{
				const uint16 EasingChannel = System->LocateEasingChannel(RootInstanceHandles[Index], SubSequenceIDs[Index]);
				if (EasingChannel != uint16(-1))
				{
					OutEntitiesToMutate.PadToNum(Index + 1, false);
					OutEntitiesToMutate[Index] = true;
				}
			}
		}
		else
		{
			for (int32 Index = 0; Index < Allocation->Num(); ++Index)
			{
				const uint16 EasingChannel = System->LocateEasingChannel(RootInstanceHandles[Index], MovieSceneSequenceID::Root);
				if (EasingChannel != uint16(-1))
				{
					OutEntitiesToMutate.PadToNum(Index + 1, false);
					OutEntitiesToMutate[Index] = true;
				}
			}
		}
	}

	void CreateMutation(FEntityManager* InEntityManager, FComponentMask* InOutEntityComponentTypes) const override
	{
		InOutEntityComponentTypes->Set(BuiltInComponents->HierarchicalEasingChannel);

		InEntityManager->GetComponents()->Factories.ComputeMutuallyInclusiveComponents(*InOutEntityComponentTypes);

		InOutEntityComponentTypes->Set(BuiltInComponents->Tags.NeedsLink);
	}

	void InitializeEntities(const FEntityRange& EntityRange, const FComponentMask& AllocationType) const override
	{
		FEntityAllocationWriteContext NewAllocation = FEntityAllocationWriteContext::NewAllocation();

		EntityManager->GetComponents()->Factories.RunInitializers(AllocationType, EntityRange);

		TComponentWriter<uint16>                        EasingChannels      = EntityRange.Allocation->WriteComponents(BuiltInComponents->HierarchicalEasingChannel, NewAllocation);
		TComponentReader<FRootInstanceHandle>           RootInstanceHandles = EntityRange.Allocation->ReadComponents(BuiltInComponents->RootInstanceHandle);
		TOptionalComponentReader<FMovieSceneSequenceID> OptSequenceIDs      = EntityRange.Allocation->TryReadComponents(BuiltInComponents->SequenceID);

		for (int32 Index = 0; Index < EntityRange.Num; ++Index)
		{
			const int32 Offset = EntityRange.ComponentStartOffset + Index;

			FMovieSceneSequenceID SequenceID = OptSequenceIDs ? OptSequenceIDs[Offset] : MovieSceneSequenceID::Root;
			const uint16 EasingChannel = System->LocateEasingChannel(RootInstanceHandles[Offset], SequenceID);
			check(EasingChannel != uint16(-1));
			EasingChannels[Offset] = EasingChannel;
		}
	}
private:

	UMovieSceneHierarchicalEasingInstantiatorSystem* System;
	FBuiltInComponentTypes* BuiltInComponents;
	FInstanceRegistry* InstanceRegistry;
};


struct FResetFinalWeightResults
{

	static void ForEachEntity(double& Result)
	{
		Result = 1.f;
	}
};

struct FEvaluateEasings
{
	static void ForEachEntity(FFrameTime EvalTime, const FEasingComponentData& Easing, double& Result)
	{
		const double EasingWeight = Easing.Section->EvaluateEasing(EvalTime);
		Result = FMath::Max(EasingWeight, 0.f);
	}
};

struct FAccumulateManualWeights
{
	static void ForEachAllocation(const FEntityAllocation* Allocation, const TReadOneOrMoreOf<double, double>& Results, double* OutAccumulatedResults)
	{
		using NumericType = TDecay<decltype(OutAccumulatedResults)>::Type;
		
		const int32 Num = Allocation->Num();

		const double* WeightResults = Results.Get<0>();
		const double* EasingResults = Results.Get<1>();

		check(WeightResults || EasingResults);

		// Have to do math
		if (WeightResults && EasingResults)
		{
			for (int32 Index = 0; Index < Num; ++Index)
			{
				OutAccumulatedResults[Index] = WeightResults[Index] * EasingResults[Index];
			}
		}
		else
		{
			FMemory::Memcpy(OutAccumulatedResults, WeightResults ? WeightResults : EasingResults, Num * sizeof(NumericType));
		}
	}
};


struct FHarvestHierarchicalEasings
{
	TSparseArray<int32>* EasingChannelToIndex;
	TArrayView<FHierarchicalEasingChannelData> ComputationData;

	FHarvestHierarchicalEasings(TSparseArray<int32>* InEasingChannelToIndex, TArray<FHierarchicalEasingChannelData>* InComputationData)
		: EasingChannelToIndex(InEasingChannelToIndex)
		, ComputationData(*InComputationData)
	{
		check(InComputationData);
	}

	// Before the task runs, initialize the results array
	void PreTask()
	{
		for (FHierarchicalEasingChannelData& Data : ComputationData)
		{
			Data.FinalResult = 1.0;
		}
	}

	// Accumulate all entities that contribute to the channel
	void ForEachEntity(double Result, uint16 EasingChannel)
	{
		const int32 ResultIndex = (*EasingChannelToIndex)[EasingChannel];
		ComputationData[ResultIndex].FinalResult *= Result;
	}

	// Multiply hierarchical weights with sub sequences
	void PostTask()
	{
		// Move forward through the results array, multiplying with parents
		// This is possible because the results array is already sorted by depth
		for (int32 Index = 0; Index < ComputationData.Num(); ++Index)
		{
			FHierarchicalEasingChannelData ChannelData = ComputationData[Index];
			if (ChannelData.ParentEasingIndex != uint16(-1))
			{
				// The parent result has already been multiplied by all its parent weights by this point
				ComputationData[Index].FinalResult *= ComputationData[ChannelData.ParentEasingIndex].FinalResult;
			}
		}
	}
};

struct FPropagateHierarchicalEasings
{
	TArrayView<const FHierarchicalEasingChannelData> ComputationData;

	TArray<double> HierarchicalResultsByChannelID;
	int32 MaxChannelNum;

	FPropagateHierarchicalEasings(TArrayView<const FHierarchicalEasingChannelData> InComputationData, const int32 InMaxChannelNum)
		: ComputationData(InComputationData)
		, MaxChannelNum(InMaxChannelNum)
	{}

	// Before the task runs, initialize the results array to avoid a double indirection during the expansion in the actual task
	void PreTask()
	{
		HierarchicalResultsByChannelID.SetNumZeroed(MaxChannelNum);

		for (int32 Index = 0; Index < ComputationData.Num(); ++Index)
		{
			const int32 ChannelIdAsIndex = ComputationData[Index].ChannelID;
			HierarchicalResultsByChannelID[ChannelIdAsIndex] = ComputationData[Index].FinalResult;
		}
	}

	void ForEachAllocation(const FEntityAllocation* Allocation, TRead<uint16> HierarchicalEasingChannels, TWrite<double> WeightAndEasingResults)
	{
		const int32 Num = Allocation->Num();
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const uint16 HierarchicalEasingChannel = HierarchicalEasingChannels[Index];

			if (HierarchicalEasingChannel != INVALID_EASING_CHANNEL && ensure(HierarchicalResultsByChannelID.IsValidIndex(HierarchicalEasingChannel)))
			{
				WeightAndEasingResults[Index] *= HierarchicalResultsByChannelID[HierarchicalEasingChannel];
			}
		}
	}
};


} // namespace UE::MovieScene


UMovieSceneHierarchicalEasingInstantiatorSystem::UMovieSceneHierarchicalEasingInstantiatorSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();
	SystemCategories = EEntitySystemCategory::Core;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineImplicitPrerequisite(UMovieSceneRootInstantiatorSystem::StaticClass(), GetClass());
		DefineImplicitPrerequisite(GetClass(), UMovieScenePropertyInstantiatorSystem::StaticClass());
		DefineImplicitPrerequisite(GetClass(), UMovieSceneInterrogatedPropertyInstantiatorSystem::StaticClass());
		DefineComponentConsumer(GetClass(), BuiltInComponents->BoundObject);
	}
}

bool UMovieSceneHierarchicalEasingInstantiatorSystem::IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const
{
	using namespace UE::MovieScene;

	return PersistentHandleToEasingChannel.Num() || InLinker->EntityManager.ContainsComponent(FBuiltInComponentTypes::Get()->HierarchicalEasingProvider);
}

void UMovieSceneHierarchicalEasingInstantiatorSystem::OnLink()
{
	EvaluatorSystem = Linker->LinkSystem<UWeightAndEasingEvaluatorSystem>();
	// Keep the evaluator system alive as long as we are alive
	Linker->SystemGraph.AddReference(this, EvaluatorSystem);
}

void UMovieSceneHierarchicalEasingInstantiatorSystem::OnUnlink()
{
	PersistentHandleToEasingChannel.Empty();
}

void UMovieSceneHierarchicalEasingInstantiatorSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	const FBuiltInComponentTypes* const BuiltInComponents = FBuiltInComponentTypes::Get();
	const FInstanceRegistry* const InstanceRegistry = Linker->GetInstanceRegistry();

	// Step 1: Create easing channels for newly created easing providers, and add a channel ID component
	{
		FEntityComponentFilter Filter;
		Filter.All({ BuiltInComponents->RootInstanceHandle, BuiltInComponents->HierarchicalEasingProvider, BuiltInComponents->Tags.NeedsLink });
		Filter.Deny({ BuiltInComponents->Tags.ImportedEntity });
		Linker->EntityManager.MutateAll(Filter, FAddEasingChannelToProviderMutation(this));
	}

	FAddEasingChannelToConsumerMutation AddEasingChannelMutation(this);

	// Step 2: Add easing ID components to any new entities that exist within a blended sequence
	{
		FEntityComponentFilter Filter;
		Filter.All({ BuiltInComponents->InstanceHandle, BuiltInComponents->Tags.NeedsLink });
		Filter.None({ BuiltInComponents->HierarchicalEasingChannel, BuiltInComponents->Tags.NeedsUnlink, BuiltInComponents->Tags.ImportedEntity });

		Linker->EntityManager.MutateConditional(Filter, AddEasingChannelMutation);
	}

	// Step 3: Add easing ID components to any pre-existing entities that exist within a sequence that just had a channel allocated
	for (const FHierarchicalKey& Key : NewEasingChannelKeys)
	{
		const FSequenceInstance& RootInstance = InstanceRegistry->GetInstance(Key.RootInstanceHandle);

		const FSequenceInstance* InstanceToCheck = nullptr;
		if (Key.SequenceID == MovieSceneSequenceID::Root)
		{
			InstanceToCheck = &RootInstance;
		}
		else
		{
			FInstanceHandle SubInstanceHandle = RootInstance.FindSubInstance(Key.SequenceID);
			if (SubInstanceHandle.IsValid())
			{
				InstanceToCheck = &InstanceRegistry->GetInstance(SubInstanceHandle);
			}
		}

		if (InstanceToCheck)
		{
			// Find any entities that have already been linked and add the easing channel to them
			FEntityComponentFilter Filter;
			Filter.None({ BuiltInComponents->Tags.NeedsLink, BuiltInComponents->Tags.NeedsUnlink, BuiltInComponents->HierarchicalEasingChannel, BuiltInComponents->Tags.ImportedEntity });

			InstanceToCheck->Ledger.MutateAll(Linker, Filter, AddEasingChannelMutation);
		}
	}

	NewEasingChannelKeys.Empty();


	RemoveUnlinkedHierarchicalEasingChannels(InPrerequisites, Subsequents);
}

void UMovieSceneHierarchicalEasingInstantiatorSystem::RemoveUnlinkedHierarchicalEasingChannels(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	const FBuiltInComponentTypes* const BuiltInComponents = FBuiltInComponentTypes::Get();
	const FInstanceRegistry* const InstanceRegistry = Linker->GetInstanceRegistry();

	// Step 3: Visit removed hierarchical easing providers, so we can free up our channels.
	//         @todo: There is a risk here that the easing provider is removed before the instance has been finished.
	//                leaving some entities hanging around that still have this channel ID assigned. This would casue an ensure in the evaluator system.
	//                This currently can't happen though because sub-sequence easing always lasts the duration of the sub-section, and dynamic weights
	//                are never removed once they are created (until the root sequence ends)
	auto VisitRemovedEasingProviders = [this, InstanceRegistry](const FEntityAllocation* Allocation, const FRootInstanceHandle* RootInstanceHandles, const FMovieSceneSequenceID* SubSequenceIDs)
	{
		for (int32 Index = 0; Index < Allocation->Num(); ++Index)
		{
			FHierarchicalKey Key { RootInstanceHandles[Index], SubSequenceIDs[Index] };

			if (FHierarchicalInstanceData* InstanceData = this->PersistentHandleToEasingChannel.Find(Key))
			{
				if (--InstanceData->RefCount == 0)
				{
					this->EvaluatorSystem->ReleaseEasingChannel(InstanceData->EasingChannelID);
					this->PersistentHandleToEasingChannel.Remove(Key);
				}
			}
		}
	};

	FEntityTaskBuilder()
	.Read(BuiltInComponents->RootInstanceHandle)
	.Read(BuiltInComponents->HierarchicalEasingProvider)
	.FilterAll({ BuiltInComponents->Tags.NeedsUnlink })
	.FilterNone({ BuiltInComponents->ParentEntity })
	.Iterate_PerAllocation(&Linker->EntityManager, VisitRemovedEasingProviders);
}

UMovieSceneHierarchicalEasingInstantiatorSystem::FHierarchicalInstanceData UMovieSceneHierarchicalEasingInstantiatorSystem::AllocateEasingChannel(const UE::MovieScene::FInstanceRegistry* InstanceRegistry, UE::MovieScene::FRootInstanceHandle RootInstanceHandle, FMovieSceneSequenceID SequenceID)
{
	using namespace UE::MovieScene;

	const FMovieSceneSequenceHierarchy* RootHierarchy = (SequenceID != MovieSceneSequenceID::Root)
		? InstanceRegistry->GetInstance(RootInstanceHandle).GetPlayer()->GetEvaluationTemplate().GetHierarchy()
		: nullptr;

	return AllocateEasingChannelImpl(RootInstanceHandle, SequenceID, RootHierarchy);
}

UMovieSceneHierarchicalEasingInstantiatorSystem::FHierarchicalInstanceData UMovieSceneHierarchicalEasingInstantiatorSystem::AllocateEasingChannelImpl(UE::MovieScene::FRootInstanceHandle RootInstanceHandle, FMovieSceneSequenceID SequenceID, const FMovieSceneSequenceHierarchy* Hierarchy)
{
	using namespace UE::MovieScene;

	FHierarchicalKey Key{ RootInstanceHandle, SequenceID };

	FHierarchicalInstanceData* ExistingInstanceData = PersistentHandleToEasingChannel.Find(Key);
	if (ExistingInstanceData != nullptr)
	{
		++ExistingInstanceData->RefCount;
		return *ExistingInstanceData;
	}

	FHierarchicalInstanceData ParentInstanceData;

	if (SequenceID != MovieSceneSequenceID::Root)
	{
		const FMovieSceneSequenceHierarchyNode* Node = Hierarchy->FindNode(SequenceID);
		check(Node);
		ParentInstanceData = AllocateEasingChannelImpl(RootInstanceHandle, Node->ParentID, Hierarchy);
	}

	FHierarchicalInstanceData NewInstanceData;
	NewInstanceData.RefCount = 1;
	NewInstanceData.HierarchicalDepth = ParentInstanceData.HierarchicalDepth + 1;
	NewInstanceData.EasingChannelID = EvaluatorSystem->AllocateEasingChannel(ParentInstanceData.EasingChannelID, NewInstanceData.HierarchicalDepth);

	PersistentHandleToEasingChannel.Add(Key, NewInstanceData);
	NewEasingChannelKeys.Add(Key);

	return NewInstanceData;
}

uint16 UMovieSceneHierarchicalEasingInstantiatorSystem::LocateEasingChannel(UE::MovieScene::FRootInstanceHandle RootInstanceHandle, FMovieSceneSequenceID SequenceID) const
{
	if (const FHierarchicalInstanceData* ExistingInstanceData = PersistentHandleToEasingChannel.Find(FHierarchicalKey{ RootInstanceHandle, SequenceID }))
	{
		return ExistingInstanceData->EasingChannelID;
	}
	return uint16(-1);
}

void UMovieSceneHierarchicalEasingInstantiatorSystem::ReleaseEasingChannel(UE::MovieScene::FRootInstanceHandle RootInstanceHandle, FMovieSceneSequenceID SequenceID)
{
	FHierarchicalKey Key{ RootInstanceHandle, SequenceID };

	const FHierarchicalInstanceData* ExistingInstanceData = PersistentHandleToEasingChannel.Find(Key);
	if (ExistingInstanceData != nullptr)
	{
		EvaluatorSystem->ReleaseEasingChannel(ExistingInstanceData->EasingChannelID);

		PersistentHandleToEasingChannel.Remove(Key);
	}
}


UWeightAndEasingEvaluatorSystem::UWeightAndEasingEvaluatorSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	SystemCategories = UE::MovieScene::EEntitySystemCategory::ChannelEvaluators;

	bResultsNeedResort = false;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		const FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

		DefineComponentConsumer(GetClass(), BuiltInComponents->WeightResult);

		DefineImplicitPrerequisite(UMovieSceneEvalTimeSystem::StaticClass(), GetClass());
		DefineComponentProducer(GetClass(), BuiltInComponents->EasingResult);
		DefineComponentProducer(GetClass(), BuiltInComponents->WeightAndEasingResult);
	}
}

bool UWeightAndEasingEvaluatorSystem::IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* Components = FBuiltInComponentTypes::Get();
	return InLinker->EntityManager.ContainsAnyComponent({ Components->WeightAndEasingResult });
}

void UWeightAndEasingEvaluatorSystem::OnLink()
{
	// Sometimes there can be easing channels left open
	// For instance if a sub sequence caused its parent to have a
	// hierarchical channel allocated even though it wasn't explicitly weighted.
	// Since there are no entities left that need the channels, we can just remove them now
	EasingChannelToIndex.Empty();
	PreAllocatedComputationData.Empty();
}

void UWeightAndEasingEvaluatorSystem::OnUnlink()
{
	// Sometimes there can be easing channels left open
	// For instance if a sub sequence caused its parent to have a
	// hierarchical channel allocated even though it wasn't explicitly weighted.
	// Since there are no entities left that need the channels, we can just remove them now
	EasingChannelToIndex.Empty();
	PreAllocatedComputationData.Empty();
}

uint16 UWeightAndEasingEvaluatorSystem::AllocateEasingChannel(const uint16 ParentEasingChannel, const uint16 HierarchicaDepth)
{
	using namespace UE::MovieScene;

	bResultsNeedResort = true;

	// Allocate the result for this channel
	const int32 ResultIndex = PreAllocatedComputationData.Num();
	const int32 EasingChannelID = EasingChannelToIndex.Add(ResultIndex);

	check(EasingChannelID >= 0 && EasingChannelID < uint16(-1));

	FHierarchicalEasingChannelData Channel;
	Channel.HierarchicalDepth = HierarchicaDepth;
	Channel.ChannelID = EasingChannelID;
	if (ParentEasingChannel != INVALID_EASING_CHANNEL)
	{
		Channel.ParentEasingIndex = EasingChannelToIndex[ParentEasingChannel];
	}

	PreAllocatedComputationData.Add(Channel);
	return static_cast<uint16>(EasingChannelID);
}

void UWeightAndEasingEvaluatorSystem::ReleaseEasingChannel(uint16 EasingChannelID)
{
	const int32 ComputationDataIndex = EasingChannelToIndex[EasingChannelID];

	// Mark the channel ID as invalid, and give it the largest hierarchical depth so it gets sorted to the end
	PreAllocatedComputationData[ComputationDataIndex].HierarchicalDepth = uint16(-1);
	EasingChannelToIndex.RemoveAt(EasingChannelID);

	bResultsNeedResort = true;
}

void UWeightAndEasingEvaluatorSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* Components = FBuiltInComponentTypes::Get();
	FInstanceRegistry* InstanceRegistry = Linker->GetInstanceRegistry();

	// No hierarchical weighting, just reset everything to 1.0
	FGraphEventRef ResetWeights = FEntityTaskBuilder()
	.Write(Components->WeightAndEasingResult)
	.SetStat(GET_STATID(MovieSceneEval_EvaluateEasingTask))
	.Dispatch_PerEntity<FResetFinalWeightResults>(&Linker->EntityManager, InPrerequisites, &Subsequents);

	FSystemTaskPrerequisites ResetWeightsDependencies {InPrerequisites};
	ResetWeightsDependencies.AddComponentTask(Components->WeightAndEasingResult, ResetWeights);

	// Step 1: Evaluate section easing and manual weights in parallel
	//
	FGraphEventRef EvaluateEasing = FEntityTaskBuilder()
	.Read(Components->EvalTime)
	.Read(Components->Easing)
	.Write(Components->EasingResult)
	.SetStat(GET_STATID(MovieSceneEval_EvaluateEasingTask))
	.Dispatch_PerEntity<FEvaluateEasings>(&Linker->EntityManager, ResetWeightsDependencies, &Subsequents);

	FGraphEventRef AccumulateManualWeights = FEntityTaskBuilder()
	.ReadOneOrMoreOf(Components->WeightResult, Components->EasingResult)
	.Write(Components->WeightAndEasingResult)
	.SetStat(GET_STATID(MovieSceneEval_EvaluateEasingTask))
	.Dispatch_PerAllocation<FAccumulateManualWeights>(&Linker->EntityManager, ResetWeightsDependencies, &Subsequents);

	// If we have hierarchical easing, we initialize all the weights to their hierarchical defaults
	if (PreAllocatedComputationData.Num() > 0)
	{
		ResortComputationBuffer();

		FSystemTaskPrerequisites HarvestPrereqs {InPrerequisites};
		HarvestPrereqs.AddComponentTask(Components->WeightAndEasingResult, EvaluateEasing);
		HarvestPrereqs.AddComponentTask(Components->WeightAndEasingResult, AccumulateManualWeights);

		// Step 2: Harvest any hierarchical results from providers
		//
		FGraphEventRef HarvestTask = FEntityTaskBuilder()
		.Read(Components->WeightAndEasingResult)
		.Read(Components->HierarchicalEasingChannel)
		.FilterAll({ Components->HierarchicalEasingProvider })  // Only harvest results from entities that are providing results
		.SetStat(GET_STATID(MovieSceneEval_HarvestEasingTask))
		.Dispatch_PerEntity<FHarvestHierarchicalEasings>(&Linker->EntityManager, HarvestPrereqs, nullptr, &EasingChannelToIndex, &PreAllocatedComputationData);

		FSystemTaskPrerequisites PropagatePrereqs {InPrerequisites};
		PropagatePrereqs.AddRootTask(HarvestTask);

		// Step 3: Apply hierarchical easing results to all entities inside affected sub-sequences.
		//
		FEntityTaskBuilder()
		.Read(Components->HierarchicalEasingChannel)
		.Write(Components->WeightAndEasingResult)
		.FilterNone({ Components->HierarchicalEasingProvider }) // Do not propagate hierarchical weights onto providers!
		.SetStat(GET_STATID(MovieSceneEval_EvaluateEasingTask))
		.Dispatch_PerAllocation<FPropagateHierarchicalEasings>(&Linker->EntityManager, PropagatePrereqs, &Subsequents,
			PreAllocatedComputationData, EasingChannelToIndex.GetMaxIndex());
	}
}

void UWeightAndEasingEvaluatorSystem::ResortComputationBuffer()
{
	using namespace UE::MovieScene;

	if (!bResultsNeedResort)
	{
		return;
	}

	bResultsNeedResort = false;

	TSparseArray<int32> OldToNewIndex;

	// Resort the results array by depth if it has been modified
	Algo::SortBy(PreAllocatedComputationData, &FHierarchicalEasingChannelData::HierarchicalDepth);

	for (int32 Index = 0; Index < PreAllocatedComputationData.Num(); ++Index)
	{
		FHierarchicalEasingChannelData& ComputationData = PreAllocatedComputationData[Index];

		// As soon as we find an invalid hierarchical depth, everything proceeding this index is garbage
		if (ComputationData.HierarchicalDepth == uint16(-1))
		{
			PreAllocatedComputationData.RemoveAt(Index, PreAllocatedComputationData.Num() - Index);
			break;
		}

		// Reassign the channel ID to index mapping
		int32& ChannelIndex = EasingChannelToIndex[ComputationData.ChannelID];
		OldToNewIndex.Insert(ChannelIndex, Index);
		ChannelIndex = Index;
		
		if (ComputationData.ParentEasingIndex != uint16(-1))
		{
			// Parent Index must have been added to the OldToNewIndex map by now because the results are always sorted
			ComputationData.ParentEasingIndex = OldToNewIndex[ComputationData.ParentEasingIndex];
		}
	}
}

