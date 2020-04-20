// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Components/BoundsCopyComponent.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "SceneInterface.h"

UBoundsCopyComponent::UBoundsCopyComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsEditorOnly = true;
}

#if WITH_EDITOR

void UBoundsCopyComponent::SetRotation()
{
	if (BoundsSourceActor.IsValid())
	{
		// Copy the source actor rotation and notify the parent actor
		GetOwner()->Modify();
		GetOwner()->SetActorRotation(BoundsSourceActor->GetTransform().GetRotation());
		GetOwner()->PostEditMove(true);
	}
}

void UBoundsCopyComponent::SetTransformToBounds()
{
	if (BoundsSourceActor.IsValid())
	{
		// Calculate the bounds in our local rotation space translated to the BoundsSourceActor center
		const FQuat TargetRotation = GetOwner()->ActorToWorld().GetRotation();
		const FVector InitialPosition = BoundsSourceActor->GetComponentsBoundingBox().GetCenter();

		FTransform LocalTransform;
		LocalTransform.SetComponents(TargetRotation, InitialPosition, FVector::OneVector);
		FTransform WorldToLocal = LocalTransform.Inverse();

		FBox BoundBox(ForceInit);
		for (const UActorComponent* Component : BoundsSourceActor->GetComponents())
		{
			// Only gather visual components in the bounds calculation
			const UPrimitiveComponent* PrimitiveComponent = Cast<const UPrimitiveComponent>(Component);
			if (PrimitiveComponent != nullptr && PrimitiveComponent->IsRegistered())
			{
				const FTransform ComponentToActor = PrimitiveComponent->GetComponentTransform() * WorldToLocal;
				FBoxSphereBounds LocalSpaceComponentBounds = PrimitiveComponent->CalcBounds(ComponentToActor);
				if (LocalSpaceComponentBounds.GetBox().GetVolume() > 0.f)
				{
					BoundBox += LocalSpaceComponentBounds.GetBox();
				}
			}
		}

		// Create transform from bounds
		FVector Origin;
		FVector Extent;
		BoundBox.GetCenterAndExtents(Origin, Extent);

		Origin = LocalTransform.TransformPosition(Origin);
		Extent *= FVector(2.f, 2.f, 1.f); // Account for ARuntimeVirtualTextureVolume:Box offset which centers it on origin

		FTransform Transform;
		Transform.SetComponents(TargetRotation, Origin, Extent);

		// Apply final result and notify the parent actor
		GetOwner()->Modify();
		GetOwner()->SetActorTransform(Transform);
		GetOwner()->PostEditMove(true);
	}
}

#endif
