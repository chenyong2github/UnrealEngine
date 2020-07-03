// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieSceneHierarchicalBiasSystem.h"

#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"


namespace UE
{
namespace MovieScene
{

struct FHierarchicalBiasTask
{
	explicit FHierarchicalBiasTask(UMovieSceneEntitySystemLinker* InLinker)
		: Linker(InLinker)
	{}

	void InitializeChannel(uint16 BlendChannel)
	{
		MaxBiasByChannel.FindOrAdd(BlendChannel, MIN_int16);
	}

	bool HasAnyWork() const
	{
		return MaxBiasByChannel.Num() != 0;
	}

	void ForEachAllocation(const FEntityAllocation* Allocation, FReadEntityIDs EntityIDAccessor, TRead<uint16> BlendChannelAcessor, TReadOptional<int16> HBiasAccessor)
	{
		TArrayView<const FMovieSceneEntityID> EntityIDs = EntityIDAccessor.ResolveAsArray(Allocation);
		TArrayView<const int16>  HBiases                = HBiasAccessor.ResolveAsArray(Allocation);
		TArrayView<const uint16> BlendChannels          = BlendChannelAcessor.ResolveAsArray(Allocation);

		if (HBiases.Num())
		{
			for (int32 Index = 0; Index < BlendChannels.Num(); ++Index)
			{
				VisitChannel(EntityIDs[Index], BlendChannels[Index], HBiases[Index]);
			}
		}
		else
		{
			for (int32 Index = 0; Index < BlendChannels.Num(); ++Index)
			{
				VisitChannel(EntityIDs[Index], BlendChannels[Index], 0);
			}
		}
	}

	void PostTask()
	{
		FBuiltInComponentTypes* Components = FBuiltInComponentTypes::Get();

		for (auto It = ActiveContributorsByChannel.CreateIterator(); It; ++It)
		{
			Linker->EntityManager.RemoveComponent(It.Value(), Components->Tags.Ignored);
		}

		for (auto It = InactiveContributorsByChannel.CreateIterator(); It; ++It)
		{
			Linker->EntityManager.AddComponent(It.Value(), Components->Tags.Ignored);
		}
	}

private:

	void VisitChannel(FMovieSceneEntityID EntityID, uint16 BlendChannel, int16 HBias)
	{
		// If this channel hasn't changed at all (ie InitializeChannel was not called for it) do nothing
		if (int16* ExistingBias = MaxBiasByChannel.Find(BlendChannel))
		{
			if (HBias > *ExistingBias)
			{
				for (auto It = ActiveContributorsByChannel.CreateKeyIterator(BlendChannel); It; ++It)
				{
					InactiveContributorsByChannel.Add(BlendChannel, It.Value());
					It.RemoveCurrent();
				}

				*ExistingBias = HBias;
				ActiveContributorsByChannel.Add(BlendChannel, EntityID);
			}
			else if (HBias == *ExistingBias)
			{
				ActiveContributorsByChannel.Add(BlendChannel, EntityID);
			}
			else
			{
				InactiveContributorsByChannel.Add(BlendChannel, EntityID);
			}
		}
	}

	TMap<uint16, int16> MaxBiasByChannel;

	TMultiMap<uint16, FMovieSceneEntityID> InactiveContributorsByChannel;

	TMultiMap<uint16, FMovieSceneEntityID> ActiveContributorsByChannel;

	UMovieSceneEntitySystemLinker* Linker;
};

} // namespace MovieScene
} // namespace UE


UMovieSceneHierarchicalBiasSystem::UMovieSceneHierarchicalBiasSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineComponentConsumer(GetClass(), FBuiltInComponentTypes::Get()->BlendChannelInput);
	}
}

bool UMovieSceneHierarchicalBiasSystem::IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* Components = FBuiltInComponentTypes::Get();
	return InLinker->EntityManager.ContainsAllComponents({ Components->BlendChannelInput, Components->HierarchicalBias });
}

void UMovieSceneHierarchicalBiasSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* Components = FBuiltInComponentTypes::Get();

	FHierarchicalBiasTask Task(Linker);

	// First, add all the channels that have changed to the map
	FEntityTaskBuilder()
	.Read(Components->BlendChannelInput)
	.FilterAny({ Components->Tags.NeedsLink, Components->Tags.NeedsUnlink })
	.Iterate_PerEntity(&Linker->EntityManager, [&Task](uint16 BlendChannel){ Task.InitializeChannel(BlendChannel); });

	if (Task.HasAnyWork())
	{
		FEntityTaskBuilder()
		.ReadEntityIDs()
		.Read(Components->BlendChannelInput)
		.ReadOptional(Components->HierarchicalBias)
		.FilterNone({ Components->Tags.NeedsUnlink })
		.RunInline_PerAllocation(&Linker->EntityManager, Task);
	}
}