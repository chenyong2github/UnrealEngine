// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieSceneComponentAttachmentSystem.h"
#include "Systems/MovieScenePropertyInstantiator.h"
#include "Systems/MovieSceneComponentTransformSystem.h"
#include "Systems/MovieSceneComponentMobilitySystem.h"

#include "EntitySystem/MovieSceneBoundObjectInstantiator.h"
#include "EntitySystem/MovieSceneBoundSceneComponentInstantiator.h"

#include "EntitySystem/BuiltInComponentTypes.h"
#include "MovieSceneTracksComponentTypes.h"

#include "GameFramework/Actor.h"

#include "MovieSceneTracksComponentTypes.h"
#include "MovieSceneObjectBindingID.h"
#include "IMovieScenePlayer.h"

namespace UE
{
namespace MovieScene
{



struct F3DAttachTokenProducer : IMovieScenePreAnimatedTokenProducer
{
	/** Cache the existing state of an object before moving it */
	virtual IMovieScenePreAnimatedTokenPtr CacheExistingState(UObject& Object) const override
	{
		struct FToken : IMovieScenePreAnimatedToken
		{
			FTransform ComponentTransform;
			FPreAnimAttachment Attachment;

			virtual void RestoreState(UObject& InObject, IMovieScenePlayer& Player) override
			{
				USceneComponent* SceneComponent = CastChecked<USceneComponent>(&InObject);
				Attachment.DetachParams.ApplyDetach(SceneComponent, Attachment.OldAttachParent.Get(), Attachment.OldAttachSocket);
			}
		};

		USceneComponent* SceneComponent = CastChecked<USceneComponent>(&Object);

		FToken Token;
		Token.Attachment.OldAttachParent = SceneComponent->GetAttachParent();
		Token.Attachment.OldAttachSocket = SceneComponent->GetAttachSocketName();
		return Token;
	}
};


struct FInitializeAttachParentsTask
{
	FInstanceRegistry* InstanceRegistry;

	void ForEachEntity(UE::MovieScene::FInstanceHandle InstanceHandle, const FMovieSceneObjectBindingID& BindingID, const UE::MovieScene::FAttachmentComponent& AttachComponent, USceneComponent*& OutAttachedParent)
	{
		const FSequenceInstance* TargetInstance = &InstanceRegistry->GetInstance(InstanceHandle);

		IMovieScenePlayer* Player = TargetInstance->GetPlayer();

		FMovieSceneSequenceID ResolvedSequenceID = TargetInstance->GetSequenceID();
		if (!TargetInstance->IsRootSequence())
		{
			ResolvedSequenceID = BindingID.ResolveLocalToRoot(TargetInstance->GetSequenceID(), *Player).GetSequenceID();
			TargetInstance = &InstanceRegistry->GetInstance(TargetInstance->GetRootInstanceHandle());
		}

		for (TWeakObjectPtr<> WeakObject : Player->FindBoundObjects(BindingID.GetGuid(), ResolvedSequenceID))
		{
			if (AActor* ParentActor = Cast<AActor>(WeakObject.Get()))
			{
				OutAttachedParent = AttachComponent.Destination.ResolveAttachment(ParentActor);

				// Can only ever be attached to one thing
				return;
			}
		}
	}
};

struct FAttachmentHandler
{
	UMovieSceneComponentAttachmentSystem* AttachmentSystem;
	FMovieSceneTracksComponentTypes* TrackComponents;
	FEntityManager* EntityManager;

	FAttachmentHandler(UMovieSceneComponentAttachmentSystem* InAttachmentSystem)
		: AttachmentSystem(InAttachmentSystem)
	{
		EntityManager = &InAttachmentSystem->GetLinker()->EntityManager;
		TrackComponents = FMovieSceneTracksComponentTypes::Get();
	}

	void InitializeOutput(UObject* Object, TArrayView<const FMovieSceneEntityID> Inputs, FPreAnimAttachment* Output, FEntityOutputAggregate Aggregate)
	{
		USceneComponent* AttachChild = CastChecked<USceneComponent>(Object);

		Output->OldAttachParent = AttachChild->GetAttachParent();
		Output->OldAttachSocket = AttachChild->GetAttachSocketName();

		UpdateOutput(Object, Inputs, Output, Aggregate);
	}

	void UpdateOutput(UObject* Object, TArrayView<const FMovieSceneEntityID> Inputs, FPreAnimAttachment* Output, FEntityOutputAggregate Aggregate)
	{
		USceneComponent* AttachChild = CastChecked<USceneComponent>(Object);

		for (FMovieSceneEntityID Entity : Inputs)
		{
			TComponentPtr<USceneComponent* const>     AttachParentComponent = EntityManager->ReadComponent(Entity, TrackComponents->AttachParent);
			TComponentPtr<const FAttachmentComponent> AttachmentComponent   = EntityManager->ReadComponent(Entity, TrackComponents->AttachComponent);
			if (AttachParentComponent && AttachmentComponent)
			{
				if (USceneComponent* AttachParent = *AttachParentComponent)
				{
					Output->DetachParams = AttachmentComponent->DetachParams;
					AttachmentComponent->AttachParams.ApplyAttach(AttachChild, AttachParent, AttachmentComponent->Destination.SocketName);

					// Can only be attached to one thing
					break;
				}
			}
		}
	}

	void DestroyOutput(UObject* Object, FPreAnimAttachment* Output, FEntityOutputAggregate Aggregate)
	{
		if (Aggregate.bNeedsRestoration)
		{
			USceneComponent* AttachChild = CastChecked<USceneComponent>(Object);
			AttachmentSystem->AddPendingDetach(AttachChild, *Output);
		}
	}
};

} // namespace MovieScene
} // namespace UE



UMovieSceneComponentAttachmentInvalidatorSystem::UMovieSceneComponentAttachmentInvalidatorSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	FMovieSceneTracksComponentTypes* TrackComponents = FMovieSceneTracksComponentTypes::Get();
	RelevantComponent = TrackComponents->AttachParentBinding;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineImplicitPrerequisite(GetClass(), UMovieSceneGenericBoundObjectInstantiator::StaticClass());
		DefineImplicitPrerequisite(GetClass(), UMovieSceneBoundSceneComponentInstantiator::StaticClass());
	}
}

void UMovieSceneComponentAttachmentInvalidatorSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	UnlinkStaleObjectBindings(FMovieSceneTracksComponentTypes::Get()->AttachParentBinding);
}

UMovieSceneComponentAttachmentSystem::UMovieSceneComponentAttachmentSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	SystemExclusionContext |= EEntitySystemContext::Interrogation;

	FMovieSceneTracksComponentTypes* TrackComponents = FMovieSceneTracksComponentTypes::Get();
	RelevantComponent = TrackComponents->AttachParentBinding;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineImplicitPrerequisite(UMovieScenePropertyInstantiatorSystem::StaticClass(), GetClass());

		DefineImplicitPrerequisite(UMovieSceneCachePreAnimatedStateSystem::StaticClass(), GetClass());
		DefineImplicitPrerequisite(UMovieSceneComponentMobilitySystem::StaticClass(), GetClass());
		DefineImplicitPrerequisite(GetClass(), UMovieSceneRestorePreAnimatedStateSystem::StaticClass());
		DefineImplicitPrerequisite(GetClass(), UMovieScenePreAnimatedComponentTransformSystem::StaticClass());

		DefineComponentConsumer(GetClass(), FBuiltInComponentTypes::Get()->BoundObject);
		DefineComponentConsumer(GetClass(), FBuiltInComponentTypes::Get()->SymbolicTags.CreatesEntities);
	}
}

void UMovieSceneComponentAttachmentSystem::OnLink()
{
	UMovieSceneRestorePreAnimatedStateSystem* RestoreSystem = Linker->LinkSystem<UMovieSceneRestorePreAnimatedStateSystem>();
	Linker->SystemGraph.AddReference(this, RestoreSystem);

	UMovieSceneComponentAttachmentInvalidatorSystem* AttachmentInvalidator = Linker->LinkSystem<UMovieSceneComponentAttachmentInvalidatorSystem>();
	Linker->SystemGraph.AddReference(this, AttachmentInvalidator);
	Linker->SystemGraph.AddPrerequisite(AttachmentInvalidator, this);

	Linker->Events.TagGarbage.AddUObject(this, &UMovieSceneComponentAttachmentSystem::TagGarbage);
}

void UMovieSceneComponentAttachmentSystem::TagGarbage(UMovieSceneEntitySystemLinker*)
{
	AttachmentTracker.CleanupGarbage();
}

void UMovieSceneComponentAttachmentSystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	CastChecked<UMovieSceneComponentAttachmentSystem>(InThis)->AttachmentTracker.AddReferencedObjects(Collector);
}

void UMovieSceneComponentAttachmentSystem::OnUnlink()
{
	using namespace UE::MovieScene;

	AttachmentTracker.Destroy(FAttachmentHandler(this));
}

void UMovieSceneComponentAttachmentSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	ensureMsgf(PendingAttachmentsToRestore.Num() == 0, TEXT("Pending attachments were not previously restored when they should have been"));

	FBuiltInComponentTypes*          Components      = FBuiltInComponentTypes::Get();
	FMovieSceneTracksComponentTypes* TrackComponents = FMovieSceneTracksComponentTypes::Get();

	// Step 1: Resolve attach parent bindings that need linking
	FInitializeAttachParentsTask InitAttachParents{ Linker->GetInstanceRegistry() };

	FEntityTaskBuilder()
	.Read(Components->InstanceHandle)
	.Read(TrackComponents->AttachParentBinding)
	.Read(TrackComponents->AttachComponent)
	.Write(TrackComponents->AttachParent)
	.FilterAll({ Components->Tags.NeedsLink })
	.RunInline_PerEntity(&Linker->EntityManager, InitAttachParents);

	// Step 2: Update all invalidated inputs and outputs for attachments
	FEntityComponentFilter Filter;
	Filter.All({ TrackComponents->AttachComponent });

	AttachmentTracker.Update(Linker, FBuiltInComponentTypes::Get()->BoundObject, Filter);
	AttachmentTracker.ProcessInvalidatedOutputs(FAttachmentHandler(this));
}

void UMovieSceneComponentAttachmentSystem::AddPendingDetach(USceneComponent* SceneComponent, const UE::MovieScene::FPreAnimAttachment& Attachment)
{
	PendingAttachmentsToRestore.Add(MakeTuple(SceneComponent, Attachment));
}

void UMovieSceneComponentAttachmentSystem::SaveGlobalPreAnimatedState(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	FMovieSceneTracksComponentTypes* TrackComponents = FMovieSceneTracksComponentTypes::Get();
	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

	static FMovieSceneAnimTypeID AnimType = FMovieSceneAnimTypeID::Unique();

	F3DAttachTokenProducer Producer;

	FInstanceRegistry* InstanceRegistry = Linker->GetInstanceRegistry();
	auto IterNewObjects = [Producer, InstanceRegistry](FInstanceHandle InstanceHandle, UObject* InObject)
	{
		IMovieScenePlayer* Player = InstanceRegistry->GetInstance(InstanceHandle).GetPlayer();
		Player->SavePreAnimatedState(*InObject, AnimType, Producer);
	};

	FEntityTaskBuilder()
	.Read(BuiltInComponents->InstanceHandle)
	.Read(BuiltInComponents->BoundObject)
	.FilterAll({ BuiltInComponents->Tags.NeedsLink, TrackComponents->AttachParent })
	.Iterate_PerEntity(&Linker->EntityManager, IterNewObjects);
}

void UMovieSceneComponentAttachmentSystem::RestorePreAnimatedState(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	for (TTuple<USceneComponent*, FPreAnimAttachment> Pair : PendingAttachmentsToRestore)
	{
		Pair.Value.DetachParams.ApplyDetach(Pair.Key, Pair.Value.OldAttachParent.Get(), Pair.Value.OldAttachSocket);
	}
	PendingAttachmentsToRestore.Empty();
}
