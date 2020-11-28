// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Component/ComponentElementWorldInterface.h"
#include "Elements/Component/ComponentElementData.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"

bool UComponentElementWorldInterface::CanEditElement(const FTypedElementHandle& InElementHandle)
{
	const UActorComponent* Component = ComponentElementDataUtil::GetComponentFromHandleChecked(InElementHandle);
	return Component && Component->IsEditableWhenInherited();
}

UWorld* UComponentElementWorldInterface::GetOwnerWorld(const FTypedElementHandle& InElementHandle)
{
	const UActorComponent* Component = ComponentElementDataUtil::GetComponentFromHandleChecked(InElementHandle);
	return Component ? Component->GetWorld() : nullptr;
}

bool UComponentElementWorldInterface::GetWorldBounds(const FTypedElementHandle& InElementHandle, FBoxSphereBounds& OutBounds)
{
	if (const UActorComponent* Component = ComponentElementDataUtil::GetComponentFromHandleChecked(InElementHandle))
	{
		if (const USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			OutBounds = SceneComponent->Bounds;
			return true;
		}
	}

	return false;
}

bool UComponentElementWorldInterface::GetWorldTransform(const FTypedElementHandle& InElementHandle, FTransform& OutTransform)
{
	if (const UActorComponent* Component = ComponentElementDataUtil::GetComponentFromHandleChecked(InElementHandle))
	{
		if (const USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			OutTransform = SceneComponent->GetComponentTransform();
			return true;
		}
	}

	return false;
}

bool UComponentElementWorldInterface::SetWorldTransform(const FTypedElementHandle& InElementHandle, const FTransform& InTransform)
{
	if (UActorComponent* Component = ComponentElementDataUtil::GetComponentFromHandleChecked(InElementHandle))
	{
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			SceneComponent->Modify();
			SceneComponent->SetWorldTransform(InTransform);
			return true;
		}
	}
	
	return false;
}
