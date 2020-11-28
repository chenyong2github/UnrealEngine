// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Actor/ActorElementWorldInterface.h"
#include "Elements/Actor/ActorElementData.h"
#include "GameFramework/Actor.h"

UWorld* UActorElementWorldInterface::GetOwnerWorld(const FTypedElementHandle& InElementHandle)
{
	const AActor* Actor = ActorElementDataUtil::GetActorFromHandle(InElementHandle);
	return Actor ? Actor->GetWorld() : nullptr;
}

bool UActorElementWorldInterface::GetWorldBounds(const FTypedElementHandle& InElementHandle, FBoxSphereBounds& OutBounds)
{
	if (const AActor* Actor = ActorElementDataUtil::GetActorFromHandle(InElementHandle))
	{
		// TODO: This was taken from FActorOrComponent, but AActor has a function to calculate bounds too...
		OutBounds = Actor->GetRootComponent()->Bounds;
		return true;
	}

	return false;
}

bool UActorElementWorldInterface::GetWorldTransform(const FTypedElementHandle& InElementHandle, FTransform& OutTransform)
{
	if (const AActor* Actor = ActorElementDataUtil::GetActorFromHandle(InElementHandle))
	{
		OutTransform = Actor->GetActorTransform();
		return true;
	}

	return false;
}

bool UActorElementWorldInterface::SetWorldTransform(const FTypedElementHandle& InElementHandle, const FTransform& InTransform)
{
	if (AActor* Actor = ActorElementDataUtil::GetActorFromHandle(InElementHandle))
	{
		Actor->Modify();
		return Actor->SetActorTransform(InTransform);
	}

	return false;
}
