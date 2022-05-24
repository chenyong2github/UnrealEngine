// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/ActorDescContainer.h"

#if WITH_EDITOR
/**
* FWorldPartitionHandleUtils
*/
TUniquePtr<FWorldPartitionActorDesc>* FWorldPartitionHandleUtils::GetActorDesc(UActorDescContainer* Container, const FGuid& ActorGuid)
{
	if (TUniquePtr<FWorldPartitionActorDesc>** ActorDescPtr = Container->ActorsByGuid.Find(ActorGuid))
	{
		return *ActorDescPtr;
	}

	return nullptr;
}

UActorDescContainer* FWorldPartitionHandleUtils::GetActorDescContainer(TUniquePtr<FWorldPartitionActorDesc>* ActorDesc)
{
	return ActorDesc ? ActorDesc->Get()->GetContainer() : nullptr;
}

bool FWorldPartitionHandleUtils::IsActorDescLoaded(FWorldPartitionActorDesc* ActorDesc)
{
	return ActorDesc->IsLoaded();
}

/**
* FWorldPartitionLoadingContext
*/
FWorldPartitionLoadingContext::FImmediate FWorldPartitionLoadingContext::DefaultContext;
FWorldPartitionLoadingContext::IContext* FWorldPartitionLoadingContext::ActiveContext = &DefaultContext;

void FWorldPartitionLoadingContext::LoadAndRegisterActor(FWorldPartitionActorDesc* ActorDesc)
{
	ActiveContext->RegisterActor(ActorDesc);
}

void FWorldPartitionLoadingContext::UnloadAndUnregisterActor(FWorldPartitionActorDesc* ActorDesc)
{
	ActiveContext->UnregisterActor(ActorDesc);
}

/**
* IContext
*/
FWorldPartitionLoadingContext::IContext::IContext()
{
	check(ActiveContext == &DefaultContext);
	ActiveContext = this;
}

FWorldPartitionLoadingContext::IContext::~IContext()
{
	check(ActiveContext == this);
	ActiveContext = &DefaultContext;
}

/**
* FImmediate
*/
void FWorldPartitionLoadingContext::FImmediate::RegisterActor(FWorldPartitionActorDesc* ActorDesc)
{
	TGuardValue<bool> IsEditorLoadingPackageGuard(GIsEditorLoadingPackage, true);

	if (AActor* Actor = ActorDesc->Load())
	{
		UActorDescContainer* Container = ActorDesc->GetContainer();
		check(Container);

		const FTransform& ContainerTransform = Container->GetInstanceTransform();
		const FTransform* ContainerTransformPtr = ContainerTransform.Equals(FTransform::Identity) ? nullptr : &ContainerTransform;

		Actor->GetLevel()->AddLoadedActor(Actor, ContainerTransformPtr);
	}
}

void FWorldPartitionLoadingContext::FImmediate::UnregisterActor(FWorldPartitionActorDesc* ActorDesc)
{
	if (AActor* Actor = ActorDesc->GetActor())
	{
		UActorDescContainer* Container = ActorDesc->GetContainer();
		check(Container);

		const FTransform& ContainerTransform = Container->GetInstanceTransform();
		const FTransform* ContainerTransformPtr = ContainerTransform.Equals(FTransform::Identity) ? nullptr : &ContainerTransform;

		Actor->GetLevel()->RemoveLoadedActor(Actor, ContainerTransformPtr);

		ActorDesc->Unload();
	}	
}

/**
* FDeferred
*/
FWorldPartitionLoadingContext::FDeferred::~FDeferred()
{
	for (auto [Container, ContainerOp] : ContainerOps)
	{
		const FTransform& ContainerTransform = Container->GetInstanceTransform();
		const FTransform* ContainerTransformPtr = ContainerTransform.Equals(FTransform::Identity) ? nullptr : &ContainerTransform;

		if (ContainerOp.Registrations.Num())
		{
			TGuardValue<bool> IsEditorLoadingPackageGuard(GIsEditorLoadingPackage, true);

			TArray<AActor*> ActorList;
			ActorList.Reserve(ContainerOp.Registrations.Num());

			ULevel* Level = nullptr;
			for (FWorldPartitionActorDesc* ActorDesc : ContainerOp.Registrations)
			{
				if (AActor* Actor = ActorDesc->GetActor())
				{
					ActorList.Add(Actor);

					ULevel* ActorLevel = Actor->GetLevel();
					check(!Level || (Level == ActorLevel));
					Level = ActorLevel;
				}
			}
			
			if (ActorList.Num())
			{
				check(Level);
				Level->AddLoadedActors(ActorList, ContainerTransformPtr);
			}
		}

		if (ContainerOp.Unregistrations.Num())
		{
			TArray<AActor*> ActorList;
			ActorList.Reserve(ContainerOp.Registrations.Num());

			ULevel* Level = nullptr;
			for (FWorldPartitionActorDesc* ActorDesc : ContainerOp.Unregistrations)
			{
				if (AActor* Actor = ActorDesc->GetActor())
				{
					ActorList.Add(Actor);

					ULevel* ActorLevel = Actor->GetLevel();
					check(!Level || (Level == ActorLevel));
					Level = ActorLevel;
				}
			}
			
			if (ActorList.Num())
			{
				check(Level);
				Level->RemoveLoadedActors(ActorList, ContainerTransformPtr);
			}

			for (FWorldPartitionActorDesc* ActorDesc : ContainerOp.Unregistrations)
			{
				ActorDesc->Unload();
			}
		}
	}
}

void FWorldPartitionLoadingContext::FDeferred::RegisterActor(FWorldPartitionActorDesc* ActorDesc)
{
	check(ActorDesc);

	UActorDescContainer* Container = ActorDesc->GetContainer();
	check(Container);

	ContainerOps.FindOrAdd(Container).Registrations.Add(ActorDesc);
	check(!ContainerOps.FindChecked(Container).Unregistrations.Contains(ActorDesc));

	ActorDesc->Load();
}

void FWorldPartitionLoadingContext::FDeferred::UnregisterActor(FWorldPartitionActorDesc* ActorDesc)
{
	check(ActorDesc);

	UActorDescContainer* Container = ActorDesc->GetContainer();
	check(Container);

	ContainerOps.FindOrAdd(Container).Unregistrations.Add(ActorDesc);
	check(!ContainerOps.FindChecked(Container).Registrations.Contains(ActorDesc));
}

/**
* FWorldPartitionHandleImpl
*/
void FWorldPartitionHandleImpl::IncRefCount(FWorldPartitionActorDesc* ActorDesc)
{
	ActorDesc->IncSoftRefCount();
}

void FWorldPartitionHandleImpl::DecRefCount(FWorldPartitionActorDesc* ActorDesc)
{
	ActorDesc->DecSoftRefCount();
}

/**
* FWorldPartitionReferenceImpl
*/
void FWorldPartitionReferenceImpl::IncRefCount(FWorldPartitionActorDesc* ActorDesc)
{
	if (ActorDesc->IncHardRefCount() == 1)
	{
		FWorldPartitionLoadingContext::LoadAndRegisterActor(ActorDesc);
	}
}

void FWorldPartitionReferenceImpl::DecRefCount(FWorldPartitionActorDesc* ActorDesc)
{
	if (ActorDesc->DecHardRefCount() == 0)
	{
		FWorldPartitionLoadingContext::UnloadAndUnregisterActor(ActorDesc);
	}
}
#endif