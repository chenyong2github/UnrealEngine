// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/WeightAndEasingEvaluatorSystem.h"
#include "Async/TaskGraphInterfaces.h"
#include "Compilation/MovieSceneCompiledDataManager.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/EntityAllocationIterator.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "Sections/MovieSceneSubSection.h"
#include "Systems/FloatChannelEvaluatorSystem.h"
#include "IMovieScenePlayer.h"
#include "MovieSceneSection.h"

#include "EntitySystem/MovieSceneEvalTimeSystem.h"

#include "Algo/Find.h"

DECLARE_CYCLE_STAT(TEXT("MovieScene: Evaluate easing"), MovieSceneEval_EvaluateEasingTask, STATGROUP_MovieSceneECS);

UMovieSceneHierarchicalEasingInstantiatorSystem::UMovieSceneHierarchicalEasingInstantiatorSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();
	RelevantComponent = BuiltInComponents->HierarchicalEasingProvider;
}

void UMovieSceneHierarchicalEasingInstantiatorSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	static constexpr uint16 INVALID_EASING_CHANNEL = uint16(-1);

	const FBuiltInComponentTypes* const BuiltInComponents = FBuiltInComponentTypes::Get();
	const FInstanceRegistry* const InstanceRegistry = Linker->GetInstanceRegistry();
	UWeightAndEasingEvaluatorSystem* const EvaluatorSystem = Linker->LinkSystem<UWeightAndEasingEvaluatorSystem>();

	// Step 1: Visit any new hierarchical easing providers (i.e. entities created by sub-sections with easing on them)
	//
	// We allocate the hierarchical easing channel for their sub-sequence.
	//
	auto VisitNewEasingProviders = [this, InstanceRegistry, EvaluatorSystem](const FEntityAllocation* Allocation, TRead<FInstanceHandle> InstanceHandleAccessor, TRead<FMovieSceneSequenceID> HierarchicalEasingProviderAccessor)
	{
		TArrayView<const FInstanceHandle> InstanceHandles = InstanceHandleAccessor.ResolveAsArray(Allocation);
		TArrayView<const FMovieSceneSequenceID> HierarchicalEasingProviders = HierarchicalEasingProviderAccessor.ResolveAsArray(Allocation);

		for (int32 Index = 0; Index < Allocation->Num(); ++Index)
		{
			const FSequenceInstance& Instance = InstanceRegistry->GetInstance(InstanceHandles[Index]);
			const FMovieSceneSequenceID SubSequenceID = HierarchicalEasingProviders[Index];

			// Before we look up the sub-sequence, we need to convert the local ID into an absolute one.
			const FMovieSceneObjectBindingID LocalSubSequenceBinding(FGuid(), SubSequenceID, EMovieSceneObjectBindingSpace::Local);
			const FMovieSceneObjectBindingID RootedSubSequenceBinding = LocalSubSequenceBinding.ResolveLocalToRoot(Instance.GetSequenceID(), *Instance.GetPlayer());
			const FInstanceHandle SubSequenceHandle = Instance.FindSubInstance(RootedSubSequenceBinding.GetSequenceID());

			// We use instance handles here because sequence IDs by themselves are only unique to a single hierarchy 
			// of sequences. If a root sequence is playing twice at the same time, there will be 2 sequence instances
			// for the same ID...
			// 
			// We allocate a new easing channel on the evaluator system, add this sub-section (provider) to the list of
			// contributors to that channel, and add the channel ID to our own map for step 2 below (this is all done
			// inside AllocateEasingChannel).
			//
			// It could happen that we already had an easing channel for this sub-sequence. This can happen in editor when
			// the user forces a re-import of the sub-section (by resizing it or whatever).
			uint16& EasingChannel = InstanceHandleToEasingChannel.FindOrAdd(SubSequenceHandle, INVALID_EASING_CHANNEL);
			if (EasingChannel == INVALID_EASING_CHANNEL)
			{
				EasingChannel = EvaluatorSystem->AllocateEasingChannel(SubSequenceHandle);
			}
			else
			{
			}
		}
	};

	FEntityTaskBuilder()
		.Read(BuiltInComponents->InstanceHandle)
		.Read(BuiltInComponents->HierarchicalEasingProvider)
		.FilterAll({ BuiltInComponents->Tags.NeedsLink })
		.Iterate_PerAllocation(&Linker->EntityManager, VisitNewEasingProviders);

	// Step 2: Visit any new entities that are inside an eased-in/out sub-sequence.
	//
	// We need to assign them to the appropriate hierarchical easing channel that we created in step 1.
	//
	auto VisitNewEasings = [this, InstanceRegistry](const FEntityAllocation* Allocation, TRead<FInstanceHandle> InstanceHandleAccessor, TWrite<uint16> HierarchicalEasingAccessor)
	{
		TArrayView<const FInstanceHandle> InstanceHandles = InstanceHandleAccessor.ResolveAsArray(Allocation);
		TArrayView<uint16> HierarchicalEasings = HierarchicalEasingAccessor.ResolveAsArray(Allocation);

		for (int32 Index = 0; Index < Allocation->Num(); ++Index)
		{
			const FInstanceHandle& InstanceHandle = InstanceHandles[Index];
			const uint16* EasingChannel = InstanceHandleToEasingChannel.Find(InstanceHandle);
			if (ensure(EasingChannel))
			{
				HierarchicalEasings[Index] = *EasingChannel;
			}
		}
	};

	FEntityTaskBuilder()
		.Read(BuiltInComponents->InstanceHandle)
		.Write(BuiltInComponents->HierarchicalEasingChannel)
		.FilterAll({ BuiltInComponents->Tags.NeedsLink })
		.Iterate_PerAllocation(&Linker->EntityManager, VisitNewEasings);

	// Step 3: Visit removed hierarchical easing providers, so we can free up our channels.
	//
	auto VisitRemovedEasingProviders = [this, InstanceRegistry, EvaluatorSystem](const FEntityAllocation* Allocation, TRead<FInstanceHandle> InstanceHandleAccessor, TRead<FMovieSceneSequenceID> HierarchicalEasingProviderAccessor)
	{
		TArrayView<const FInstanceHandle> InstanceHandles = InstanceHandleAccessor.ResolveAsArray(Allocation);
		TArrayView<const FMovieSceneSequenceID> HierarchicalEasingProviders = HierarchicalEasingProviderAccessor.ResolveAsArray(Allocation);

		for (int32 Index = 0; Index < Allocation->Num(); ++Index)
		{
			const FSequenceInstance& Instance = InstanceRegistry->GetInstance(InstanceHandles[Index]);
			const FMovieSceneSequenceID SubSequenceID = HierarchicalEasingProviders[Index];
			const FInstanceHandle SubSequenceHandle = Instance.FindSubInstance(SubSequenceID);

			uint16 OutEasingChannel;
			const bool bRemoved = InstanceHandleToEasingChannel.RemoveAndCopyValue(SubSequenceHandle, OutEasingChannel);
			if (ensure(bRemoved))
			{
				EvaluatorSystem->ReleaseEasingChannel(OutEasingChannel);
			}
		}
	};

	FEntityTaskBuilder()
		.Read(BuiltInComponents->InstanceHandle)
		.Read(BuiltInComponents->HierarchicalEasingProvider)
		.FilterAll({ BuiltInComponents->Tags.NeedsUnlink })
		.FilterNone({ BuiltInComponents->ParentEntity })
		.Iterate_PerAllocation(&Linker->EntityManager, VisitRemovedEasingProviders);

}

namespace UE
{
namespace MovieScene
{

struct FEvaluateEasings
{
	UWeightAndEasingEvaluatorSystem* EvaluatorSystem;

	FEvaluateEasings(UWeightAndEasingEvaluatorSystem* InEvaluatorSystem)
		: EvaluatorSystem(InEvaluatorSystem)
	{
		check(EvaluatorSystem);
	}

	void ForEachAllocation(
			const FEntityAllocation* InAllocation, 
			TRead<FFrameTime> TimeData,
			TReadOptional<FEasingComponentData> EasingData,
			TReadOptional<float> WeightData,
			TReadOptional<FInstanceHandle> InstanceHandleData,
			TReadOptional<FMovieSceneSequenceID> HierarchicalEasingProviderData,
			TWrite<float> WeightAndEasingResultData)
	{
		const int32 Num          = InAllocation->Num();
		const FFrameTime* Times  = TimeData.Resolve(InAllocation);
		float* Results           = WeightAndEasingResultData.Resolve(InAllocation);

		// Initialize our result array.
		{
			float* CurResult = Results;
			for (int32 Idx = 0; Idx < Num; ++Idx)
			{
				*(CurResult++) = 1.f;
			}
		}

		// Compute and add easing weight.
		const bool bHasEasing = InAllocation->HasComponent(EasingData.ComponentType);
		if (bHasEasing)
		{
			float* CurResult = Results;
			const FFrameTime* CurTime = Times;
			const FEasingComponentData* CurEasing = EasingData.Resolve(InAllocation);
			for (int32 Idx = 0; Idx < Num; ++Idx)
			{
				const float EasingWeight = (CurEasing++)->Section->EvaluateEasing(*(CurTime++));
				*(CurResult++) *= FMath::Max(EasingWeight, 0.f);
			}
		}

		// Manual weight has already been computed by the float channel evaluator system, so we
		// just need to pick up the result and combine it.
		const bool bHasWeight = InAllocation->HasComponent(WeightData.ComponentType);
		if (bHasWeight)
		{
			float* CurResult = Results;
			const FFrameTime* CurTime = Times;
			const float* CurWeight = WeightData.Resolve(InAllocation);
			for (int32 Idx = 0; Idx < Num; ++Idx)
			{
				const float CustomWeight = *(CurWeight++);
				*(CurResult++) *= FMath::Max(CustomWeight, 0.f);
			}
		}

		// If this is an allocation for sub-sections that provide some ease-in/out to their child sub-sequence,
		// we store the resulting weight/easing results in the corresponding hierarhical easing channel data.
		// This will let us later apply those values onto all entities in the hierarchy below.
		// Sadly, this goes into random data access.
		//
		// Note that we need to check for instance handles because in interrogation evaluations, there are no
		// instance handles.
		//
		const bool bIsProviders = InAllocation->HasComponent(HierarchicalEasingProviderData.ComponentType);
		const bool bHasInstances = InAllocation->HasComponent(InstanceHandleData.ComponentType);
		if (bIsProviders && bHasInstances)
		{
			const FInstanceRegistry* InstanceRegistry = EvaluatorSystem->GetLinker()->GetInstanceRegistry();

			float* CurResult = Results;
			const FInstanceHandle* InstanceHandles = InstanceHandleData.Resolve(InAllocation);
			const FMovieSceneSequenceID* CurSubSequenceIDs = HierarchicalEasingProviderData.Resolve(InAllocation);
			for (int32 Idx = 0; Idx < Num; ++Idx)
			{
				const FSequenceInstance& Instance = InstanceRegistry->GetInstance(*(InstanceHandles++));
				const FInstanceHandle CurSubSequenceHandle = Instance.FindSubInstance(*(CurSubSequenceIDs++));
				EvaluatorSystem->SetSubSequenceEasing(CurSubSequenceHandle, *(CurResult++));
			}
		}
	}
};

struct FAccumulateHierarchicalEasings
{
	TSparseArray<FHierarchicalEasingChannelData>* EasingChannels;
	
	FAccumulateHierarchicalEasings(TSparseArray<FHierarchicalEasingChannelData>* InEasingChannels)
		: EasingChannels(InEasingChannels)
	{}

	FORCEINLINE TStatId           GetStatId() const    { return GET_STATID(MovieSceneEval_EvaluateEasingTask); }
	static ENamedThreads::Type    GetDesiredThread()   { return ENamedThreads::AnyThread; }
	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		Run();
	}

	void Run()
	{
		for (FHierarchicalEasingChannelData& EasingChannel : *EasingChannels)
		{
			EasingChannel.FinalEasingResult = 1.f;
			for (FHierarchicalEasingChannelContributorData& EasingChannelContributor : EasingChannel.Contributors)
			{
				EasingChannel.FinalEasingResult *= EasingChannelContributor.EasingResult;
			}
		}
	}
};

struct FPropagateHierarchicalEasings
{
	const FInstanceRegistry* InstanceRegistry;
	TSparseArray<FHierarchicalEasingChannelData>* EasingChannels;

	FPropagateHierarchicalEasings(
			const FInstanceRegistry* InInstanceRegistry, 
			TSparseArray<FHierarchicalEasingChannelData>* InEasingChannels)
		: InstanceRegistry(InInstanceRegistry)
		, EasingChannels(InEasingChannels)
	{}

	void ForEachAllocation(
			const FEntityAllocation* Allocation,
			TRead<FInstanceHandle> InstanceHandleData,
			TRead<uint16> HierarchicalEasingChannelAccessor,
			TWrite<float> WeightAndEasingResultData)
	{
		TArrayView<const FInstanceHandle> InstanceHandles = InstanceHandleData.ResolveAsArray(Allocation);
		TArrayView<const uint16> HierarchicalEasingChannels = HierarchicalEasingChannelAccessor.ResolveAsArray(Allocation);
		TArrayView<float> WeightAndEasingResults = WeightAndEasingResultData.ResolveAsArray(Allocation);

		for (int32 Index = 0; Index < Allocation->Num(); ++Index)
		{
			const FSequenceInstance& SequenceInstance = InstanceRegistry->GetInstance(InstanceHandles[Index]);
			const uint16 HierarchicalEasingChannel = HierarchicalEasingChannels[Index];
			float& WeightAndEasingResult = WeightAndEasingResults[Index];

			if (ensure(EasingChannels->IsValidIndex(HierarchicalEasingChannel)))
			{
				const FHierarchicalEasingChannelData& EasingChannel = (*EasingChannels)[HierarchicalEasingChannel];
				WeightAndEasingResult *= EasingChannel.FinalEasingResult;
			}
		}
	}
};

} // namespace MovieScene
} // namespace UE

UWeightAndEasingEvaluatorSystem::UWeightAndEasingEvaluatorSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineImplicitPrerequisite(UMovieSceneEvalTimeSystem::StaticClass(), GetClass());
		DefineImplicitPrerequisite(UFloatChannelEvaluatorSystem::StaticClass(), GetClass());
	}
}

bool UWeightAndEasingEvaluatorSystem::IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* Components = FBuiltInComponentTypes::Get();
	return InLinker->EntityManager.ContainsAnyComponent({ Components->Easing, Components->WeightResult });
}

uint16 UWeightAndEasingEvaluatorSystem::AllocateEasingChannel(UE::MovieScene::FInstanceHandle SubSequenceHandle)
{
	using namespace UE::MovieScene;

	// Create a new data for the new channel.
	FHierarchicalEasingChannelData NewEasingChannelData;
	NewEasingChannelData.Contributors.Add({ SubSequenceHandle, 1.f });

	const FSequenceInstance& SubSequenceInstance = Linker->GetInstanceRegistry()->GetInstance(SubSequenceHandle);
	const FMovieSceneRootEvaluationTemplateInstance& RootEvalTemplate = SubSequenceInstance.GetPlayer()->GetEvaluationTemplate();
	TArray<FInstanceHandle> SubSequenceParentage;
	RootEvalTemplate.GetSequenceParentage(SubSequenceHandle, SubSequenceParentage);
	for (const FInstanceHandle& ParentHandle : SubSequenceParentage)
	{
		NewEasingChannelData.Contributors.Add({ ParentHandle, 1.f });
	}

	return EasingChannels.Add(NewEasingChannelData);
}

void UWeightAndEasingEvaluatorSystem::ReleaseEasingChannel(uint16 EasingChannelID)
{
	if (ensure(EasingChannels.IsValidIndex(EasingChannelID)))
	{
		EasingChannels.RemoveAt(EasingChannelID);
	}
}

void UWeightAndEasingEvaluatorSystem::SetSubSequenceEasing(UE::MovieScene::FInstanceHandle SubSequenceHandle, float EasingResult)
{
	using namespace UE::MovieScene;

	// The given sub-sequence has been assigned the given easing value. We can copy that value everywhere this
	// sub-sequence is used in a channel, i.e. for the channel of the sub-sequence itself, but also for the channels
	// of any children sub-sequences under that sub-sequence.
	for (FHierarchicalEasingChannelData& EasingChannel : EasingChannels)
	{
		for (FHierarchicalEasingChannelContributorData& EasingChannelContributor : EasingChannel.Contributors)
		{
			if (EasingChannelContributor.SubSequenceHandle == SubSequenceHandle)
			{
				EasingChannelContributor.EasingResult = EasingResult;
				break;
			}
		}
	}
}

void UWeightAndEasingEvaluatorSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* Components = FBuiltInComponentTypes::Get();
	FInstanceRegistry* InstanceRegistry = Linker->GetInstanceRegistry();

	// Step 1: Compute all the easings and weights of all entities that have any.
	//
	FGraphEventRef EvalTask = FEntityTaskBuilder()
		// We need the eval time to evaluate easing curves.
		.Read(Components->EvalTime)
		.ReadOptional(Components->Easing)
		// We may need to multiply easing and manual weight together.
		.ReadOptional(Components->WeightResult)
		// For hierarchical easing we need the following 2 components... InstanceHandle is optional
		// because in interrogation evaluations, there are not instance handles.
		.ReadOptional(Components->InstanceHandle)
		.ReadOptional(Components->HierarchicalEasingProvider)
		// We will write the result to a separate component.
		.Write(Components->WeightAndEasingResult)
		.SetStat(GET_STATID(MovieSceneEval_EvaluateEasingTask))
		.Dispatch_PerAllocation<FEvaluateEasings>(&Linker->EntityManager, InPrerequisites, &Subsequents, this);

	// If we have no hierarchical easing, there's only one step... otherwise, we have more work to do.
	
	if (EasingChannels.Num() > 0)
	{
		// Step 2: Gather and compute sub-sequences' hierarchical easing results.
		//
		// Now, some of the entities we processed above happen to be representing sub-sections which contain entire sub-sequences.
		// We need to take their weight/easing result and propagate it to all the entities in these sub-sequences, and keep
		// propagating that down the hierarchy.
		//
		FSystemTaskPrerequisites PropagatePrereqs;

		if (Linker->EntityManager.GetThreadingModel() == EEntityThreadingModel::NoThreading)
		{
			FAccumulateHierarchicalEasings(&EasingChannels).Run();
		}
		else
		{
			FGraphEventArray AccumulatePrereqs { EvalTask };
			FGraphEventRef AccumulateTask = TGraphTask<FAccumulateHierarchicalEasings>::CreateTask(&AccumulatePrereqs, Linker->EntityManager.GetDispatchThread())
				.ConstructAndDispatchWhenReady(&EasingChannels);

			PropagatePrereqs.AddMasterTask(AccumulateTask);
		}

		// Step 3: Apply hierarchical easing results to all entities inside affected sub-sequences.
		//
		FEntityTaskBuilder()
			.Read(Components->InstanceHandle)
			.Read(Components->HierarchicalEasingChannel)
			.Write(Components->WeightAndEasingResult)
			.SetStat(GET_STATID(MovieSceneEval_EvaluateEasingTask))
			.Dispatch_PerAllocation<FPropagateHierarchicalEasings>(&Linker->EntityManager, PropagatePrereqs, &Subsequents, InstanceRegistry, &EasingChannels);
	}
}

