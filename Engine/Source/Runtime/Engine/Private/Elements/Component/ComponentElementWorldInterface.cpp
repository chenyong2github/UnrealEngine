// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Component/ComponentElementWorldInterface.h"
#include "Elements/Component/ComponentElementData.h"
#include "Components/PrimitiveComponent.h"

#include "Elements/Actor/ActorElementWorldInterface.h"

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

bool UComponentElementWorldInterface::GetBounds(const FTypedElementHandle& InElementHandle, FBoxSphereBounds& OutBounds)
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

bool UComponentElementWorldInterface::GetRelativeTransform(const FTypedElementHandle& InElementHandle, FTransform& OutTransform)
{
	if (const UActorComponent* Component = ComponentElementDataUtil::GetComponentFromHandleChecked(InElementHandle))
	{
		if (const USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			OutTransform = SceneComponent->GetRelativeTransform();
			return true;
		}
	}

	return false;
}

bool UComponentElementWorldInterface::SetRelativeTransform(const FTypedElementHandle& InElementHandle, const FTransform& InTransform)
{
	if (UActorComponent* Component = ComponentElementDataUtil::GetComponentFromHandleChecked(InElementHandle))
	{
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			SceneComponent->Modify();
			SceneComponent->SetRelativeTransform(InTransform);
			return true;
		}
	}

	return false;
}

bool UComponentElementWorldInterface::FindSuitableTransformAlongPath(const FTypedElementHandle& InElementHandle, const FVector& InPathStart, const FVector& InPathEnd, const FCollisionShape& InTestShape, TArrayView<const FTypedElementHandle> InElementsToIgnore, FTransform& OutSuitableTransform)
{
	if (const UActorComponent* Component = ComponentElementDataUtil::GetComponentFromHandleChecked(InElementHandle))
	{
		if (const UWorld* World = Component->GetWorld())
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(FindSuitableTransformAlongPath), false);

			// Don't hit ourself
			if (const UPrimitiveComponent* PrimComponent = Cast<UPrimitiveComponent>(Component))
			{
				Params.AddIgnoredComponent(PrimComponent);
			}

			return UActorElementWorldInterface::FindSuitableTransformAlongPath_WorldSweep(World, InPathStart, InPathEnd, InTestShape, InElementsToIgnore, Params, OutSuitableTransform);
		}
	}

	return false;
}
