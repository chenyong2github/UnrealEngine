// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/MovieSceneBoundObjectInstantiator.h"
#include "EntitySystem/MovieSceneEntityInstantiatorSystem.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneEntityManager.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneInstanceRegistry.h"
#include "EntitySystem/MovieSceneComponentRegistry.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneEntityFactoryTemplates.h"

#include "IMovieScenePlayer.h"

UMovieSceneGenericBoundObjectInstantiator::UMovieSceneGenericBoundObjectInstantiator(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* Components = FBuiltInComponentTypes::Get();
	RelevantComponent = Components->GenericObjectBinding;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineComponentProducer(GetClass(), Components->BoundObject);
	}
}

void UMovieSceneGenericBoundObjectInstantiator::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* Components = FBuiltInComponentTypes::Get();

	UnlinkStaleObjectBindings(Components->GenericObjectBinding);

	struct FGenericBoundObjectBatch : FObjectFactoryBatch
	{
		virtual void ResolveObjects(FInstanceRegistry* InstanceRegistry, FInstanceHandle InstanceHandle, int32 InEntityIndex, const FGuid& ObjectBinding) override
		{
			FSequenceInstance& SequenceInstance = InstanceRegistry->MutateInstance(InstanceHandle);
			for (TWeakObjectPtr<> WeakObject : SequenceInstance.GetPlayer()->FindBoundObjects(ObjectBinding, SequenceInstance.GetSequenceID()))
			{
				if (UObject* Object = WeakObject.Get())
				{
					if (!ensureMsgf(!FBuiltInComponentTypes::IsBoundObjectGarbage(Object), TEXT("Attempting to bind an object that is garbage or unreachable")))
					{
						continue;
					}

					// Make a child entity for this resolved binding
					Add(InEntityIndex, Object);
				}
			}
		}
	};

	TBoundObjectTask<FGenericBoundObjectBatch> BoundObjectTask(Linker);

	// Gather all newly instanced entities with an object binding ID
	FEntityTaskBuilder()
	.ReadEntityIDs()
	.Read(Components->InstanceHandle)
	.Read(Components->GenericObjectBinding)
	.FilterAll({ Components->Tags.NeedsLink })
	.RunInline_PerAllocation(&Linker->EntityManager, BoundObjectTask);
}
