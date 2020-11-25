// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Actor/ActorElementWorldInterface.h"
#include "Elements/Actor/ActorElementData.h"
#include "GameFramework/Actor.h"

UWorld* UActorElementWorldInterface::GetOwnerWorld(const FTypedElementHandle& InElementHandle)
{
	const FActorElementData* ActorData = InElementHandle.GetData<FActorElementData>();
	return ActorData ? ActorData->Actor->GetWorld() : nullptr;
}

bool UActorElementWorldInterface::GetWorldBounds(const FTypedElementHandle& InElementHandle, FBoxSphereBounds& OutBounds)
{
	if (const FActorElementData* ActorData = InElementHandle.GetData<FActorElementData>())
	{
		// TODO: This was taken from FActorOrComponent, but AActor has a function to calculate bounds too...
		OutBounds = ActorData->Actor->GetRootComponent()->Bounds;
		return true;
	}

	return false;
}

bool UActorElementWorldInterface::GetWorldTransform(const FTypedElementHandle& InElementHandle, FTransform& OutTransform)
{
	if (const FActorElementData* ActorData = InElementHandle.GetData<FActorElementData>())
	{
		OutTransform = ActorData->Actor->GetActorTransform();
		return true;
	}

	return false;
}

bool UActorElementWorldInterface::SetWorldTransform(const FTypedElementHandle& InElementHandle, const FTransform& InTransform)
{
	if (const FActorElementData* ActorData = InElementHandle.GetData<FActorElementData>())
	{
		ActorData->Actor->Modify();
		return ActorData->Actor->SetActorTransform(InTransform);
	}

	return false;
}
