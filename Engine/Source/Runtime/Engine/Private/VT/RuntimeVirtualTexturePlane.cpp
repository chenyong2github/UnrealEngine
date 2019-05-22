// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VT/RuntimeVirtualTexturePlane.h"

#include "Components/BoxComponent.h"
#include "VT/RuntimeVirtualTextureNotify.h"


ARuntimeVirtualTexturePlane::ARuntimeVirtualTexturePlane(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RootComponent = VirtualTextureComponent = CreateDefaultSubobject<URuntimeVirtualTextureComponent>(TEXT("VirtualTextureComponent"));

#if WITH_EDITORONLY_DATA
	// Add box for visualization of bounds
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetBoxExtent(FVector(0.5f, 0.5f, 1.f), false);
	Box->SetIsVisualizationComponent(true);
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->SetCanEverAffectNavigation(false);
	Box->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;
	Box->SetGenerateOverlapEvents(false);
	Box->SetupAttachment(VirtualTextureComponent);
#endif
}

URuntimeVirtualTextureComponent::URuntimeVirtualTextureComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bNotifyInNextTick(false)
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
		bNotifyInNextTick = true;
	}

	Super::CreateRenderState_Concurrent();
}

void URuntimeVirtualTextureComponent::SendRenderTransform_Concurrent()
{
	if (ShouldRender() && VirtualTexture != nullptr)
	{
		// This will modify the URuntimeVirtualTexture and allocate its VT
		GetScene()->AddRuntimeVirtualTexture(this);
		bNotifyInNextTick = true;
	}

	Super::SendRenderTransform_Concurrent();
}

void URuntimeVirtualTextureComponent::DestroyRenderState_Concurrent()
{
	// This will modify the URuntimeVirtualTexture and free its VT
	GetScene()->RemoveRuntimeVirtualTexture(this);
	bNotifyInNextTick = true;

	Super::DestroyRenderState_Concurrent();
}

void URuntimeVirtualTextureComponent::NotifyMaterials()
{
	if (bNotifyInNextTick)
	{
		// Notify materials of reallocation of the virtual texture caused by render state update.
		// This is slow and ideally we will find a different approach.
		RuntimeVirtualTexture::NotifyMaterials(VirtualTexture);
		bNotifyInNextTick = false;
	}
}

void URuntimeVirtualTextureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	NotifyMaterials();
}

void URuntimeVirtualTextureComponent::OnUnregister()
{
	NotifyMaterials();
	Super::OnUnregister();
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
