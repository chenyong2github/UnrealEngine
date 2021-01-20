// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/MovieSceneBoundSceneComponentInstantiator.h"
#include "EntitySystem/MovieSceneEntityInstantiatorSystem.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneEntityManager.h"
#include "EntitySystem/MovieSceneEntityFactoryTemplates.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneInstanceRegistry.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"

#include "MovieSceneCommonHelpers.h"
#include "IMovieScenePlayer.h"

#include "Components/SceneComponent.h"

UMovieSceneBoundSceneComponentInstantiator::UMovieSceneBoundSceneComponentInstantiator(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* Components = FBuiltInComponentTypes::Get();
	RelevantComponent = Components->SceneComponentBinding;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineComponentProducer(GetClass(), Components->BoundObject);
	}
}

void UMovieSceneBoundSceneComponentInstantiator::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* Components = FBuiltInComponentTypes::Get();

	UnlinkStaleObjectBindings(Components->SceneComponentBinding);

	struct FBoundSceneComponentBatch : FObjectFactoryBatch
	{
		virtual void ResolveObjects(FInstanceRegistry* InstanceRegistry, FInstanceHandle InstanceHandle, int32 InEntityIndex, const FGuid& ObjectBinding) override
		{
			FSequenceInstance& SequenceInstance = InstanceRegistry->MutateInstance(InstanceHandle);

			for (TWeakObjectPtr<> WeakObject : SequenceInstance.GetPlayer()->FindBoundObjects(ObjectBinding, SequenceInstance.GetSequenceID()))
			{
				if (UObject* Object = WeakObject.Get())
				{
					if (USceneComponent* SceneComponent = MovieSceneHelpers::SceneComponentFromRuntimeObject(Object))
					{
						if (!ensureMsgf(!FBuiltInComponentTypes::IsBoundObjectGarbage(SceneComponent), TEXT("Attempting to bind an object that is garbage or unreachable")))
						{
							continue;
						}

						// Make a child entity for this resolved binding
						Add(InEntityIndex, SceneComponent);
					}
					else if (Object->Implements<UMovieSceneSceneComponentImpersonator>())
					{
						// Objects meant explicitly to be impersonators are also allowed.
						Add(InEntityIndex, Object);
					}
				}
			}
		}
	};

	TBoundObjectTask<FBoundSceneComponentBatch> ObjectBindingTask(Linker);

	// Gather all newly instanced entities with an object binding ID
	FEntityTaskBuilder()
	.ReadEntityIDs()
	.Read(Components->InstanceHandle)
	.Read(Components->SceneComponentBinding)
	.FilterAll({ Components->Tags.NeedsLink })
	.RunInline_PerAllocation(&Linker->EntityManager, ObjectBindingTask);
}
