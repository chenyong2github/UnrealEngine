// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Component/ComponentElementWorldInterface.h"
#include "Elements/Component/ComponentElementData.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"

bool UComponentElementWorldInterface::CanEditElement(const FTypedElementHandle& InElementHandle)
{
	const FComponentElementData* ComponentData = InElementHandle.GetData<FComponentElementData>();
	return ComponentData && ComponentData->Component->IsEditableWhenInherited();
}

UWorld* UComponentElementWorldInterface::GetOwnerWorld(const FTypedElementHandle& InElementHandle)
{
	const FComponentElementData* ComponentData = InElementHandle.GetData<FComponentElementData>();
	return ComponentData ? ComponentData->Component->GetWorld() : nullptr;
}

bool UComponentElementWorldInterface::GetWorldBounds(const FTypedElementHandle& InElementHandle, FBoxSphereBounds& OutBounds)
{
	if (const FComponentElementData* ComponentData = InElementHandle.GetData<FComponentElementData>())
	{
		if (const USceneComponent* SceneComponent = Cast<USceneComponent>(ComponentData->Component))
		{
			OutBounds = SceneComponent->Bounds;
			return true;
		}
	}

	return false;
}

bool UComponentElementWorldInterface::GetWorldTransform(const FTypedElementHandle& InElementHandle, FTransform& OutTransform)
{
	if (const FComponentElementData* ComponentData = InElementHandle.GetData<FComponentElementData>())
	{
		if (const USceneComponent* SceneComponent = Cast<USceneComponent>(ComponentData->Component))
		{
			OutTransform = SceneComponent->GetComponentTransform();
			return true;
		}
	}

	return false;
}

bool UComponentElementWorldInterface::SetWorldTransform(const FTypedElementHandle& InElementHandle, const FTransform& InTransform)
{
	if (const FComponentElementData* ComponentData = InElementHandle.GetData<FComponentElementData>())
	{
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(ComponentData->Component))
		{
			SceneComponent->Modify();
			SceneComponent->SetWorldTransform(InTransform);
			return true;
		}
	}
	
	return false;
}
