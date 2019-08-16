// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Components/RuntimeVirtualTextureComponent.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "SceneInterface.h"

URuntimeVirtualTextureComponent::URuntimeVirtualTextureComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SceneProxy(nullptr)
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = true;
}

void URuntimeVirtualTextureComponent::CreateRenderState_Concurrent()
{
	if (ShouldRender() && VirtualTexture != nullptr)
	{
		// This will modify the URuntimeVirtualTexture and allocate its VT
		GetScene()->AddRuntimeVirtualTexture(this);
	}

	Super::CreateRenderState_Concurrent();
}

void URuntimeVirtualTextureComponent::SendRenderTransform_Concurrent()
{
	if (ShouldRender() && VirtualTexture != nullptr)
	{
		// This will modify the URuntimeVirtualTexture and allocate its VT
		GetScene()->AddRuntimeVirtualTexture(this);
	}

	Super::SendRenderTransform_Concurrent();
}

void URuntimeVirtualTextureComponent::DestroyRenderState_Concurrent()
{
	// This will modify the URuntimeVirtualTexture and free its VT
	GetScene()->RemoveRuntimeVirtualTexture(this);

	Super::DestroyRenderState_Concurrent();
}

#if WITH_EDITOR

void URuntimeVirtualTextureComponent::SetRotation()
{
	if (BoundsSourceActor != nullptr)
	{
		// Copy the source actor rotation and notify the parent actor
		SetWorldRotation(BoundsSourceActor->GetTransform().GetRotation());
		GetOwner()->PostEditMove(true);
	}
}

void URuntimeVirtualTextureComponent::SetTransformToBounds()
{
	if (BoundsSourceActor != nullptr)
	{
		// Calculate the bounds in our local rotation space translated to the BoundsSourceActor center
		const FQuat TargetRotation = GetComponentToWorld().GetRotation();
		const FVector InitialPosition = BoundsSourceActor->GetComponentsBoundingBox().GetCenter();
		const FVector InitialScale = FVector(0.5f, 0.5, 1.f);

		FTransform LocalTransform;
		LocalTransform.SetComponents(TargetRotation, InitialPosition, InitialScale);
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
				BoundBox += LocalSpaceComponentBounds.GetBox();
			}
		}

		// Create transform from bounds
		FVector Origin;
		FVector Extent;
		BoundBox.GetCenterAndExtents(Origin, Extent);

		Origin = LocalTransform.TransformPosition(Origin);

		FTransform Transform;
		Transform.SetComponents(TargetRotation, Origin, Extent);

		// Apply final result and notify the parent actor
		SetWorldTransform(Transform);
		GetOwner()->PostEditMove(true);
	}
}

#endif
