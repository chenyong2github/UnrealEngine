// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RuntimeVirtualTexturePlane.h"

#include "Components/BoxComponent.h"
#include "RuntimeVirtualTextureProducer.h"
#include "VT/RuntimeVirtualTexture.h"


ARuntimeVirtualTexturePlane::ARuntimeVirtualTexturePlane(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));

#if WITH_EDITORONLY_DATA
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetBoxExtent(FVector(0.5f, 0.5f, 1.f), false);
	Box->SetIsVisualizationComponent(true);
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->SetCanEverAffectNavigation(false);
	Box->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;
	Box->SetGenerateOverlapEvents(false);
	Box->SetupAttachment(RootComponent);
#endif
}

void ARuntimeVirtualTexturePlane::UpdateVirtualTexture()
{
	if (VirtualTexture != nullptr)
	{
		// The Producer object created here will be passed into the Virtual Texture system which will take ownership
		FVTProducerDescription Desc;
		VirtualTexture->GetProducerDescription(Desc);

		const ERuntimeVirtualTextureMaterialType MaterialType = VirtualTexture->GetMaterialType();

		// Transform is based on bottom left of the box
		FTransform Transform = FTransform(FVector(-0.5f, -0.5f, 0.f)) * GetTransform();
		
		FRuntimeVirtualTextureProducer* Producer = new FRuntimeVirtualTextureProducer(Desc, MaterialType, RootComponent->GetScene(), Transform);
		VirtualTexture->Initialize(Producer, Transform);
		
#if WITH_EDITOR
		// Bind function to ensure we call ReInit again if the virtual texture properties are modified
		static const FName BinderFunction(TEXT("OnVirtualTextureEditProperty"));
		VirtualTexture->OnEditProperty.BindUFunction(this, BinderFunction);
#endif
	}
}

void ARuntimeVirtualTexturePlane::ReleaseVirtualTexture()
{
	if (VirtualTexture != nullptr)
	{
		VirtualTexture->Release();

#if WITH_EDITOR
		VirtualTexture->OnEditProperty.Unbind();
#endif
	}
}

void ARuntimeVirtualTexturePlane::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	UpdateVirtualTexture();
}

void ARuntimeVirtualTexturePlane::PostLoad()
{
	Super::PostLoad();
	UpdateVirtualTexture();
}

void ARuntimeVirtualTexturePlane::BeginDestroy()
{
	ReleaseVirtualTexture();
	Super::BeginDestroy();
}

#if WITH_EDITOR

void ARuntimeVirtualTexturePlane::OnVirtualTextureEditProperty(URuntimeVirtualTexture const* InVirtualTexture)
{
	if (InVirtualTexture == VirtualTexture)
	{
		UpdateVirtualTexture();
	}
}

void ARuntimeVirtualTexturePlane::PostEditMove(bool bFinished)
{
	if (bFinished)
	{
		UpdateVirtualTexture();
	}
	Super::PostEditMove(bFinished);
}

void ARuntimeVirtualTexturePlane::SetRotation()
{
	if (SourceActor != nullptr)
	{
		RootComponent->SetWorldRotation(SourceActor->GetTransform().GetRotation());

		// Update the virtual texture to match the new transform
		UpdateVirtualTexture();
	}
}

void ARuntimeVirtualTexturePlane::SetTransformToBounds()
{
	if (SourceActor != nullptr)
	{
		// Calculate the bounds in our local rotation space translated to the SourceActor center
		const FQuat TargetRotation = GetTransform().GetRotation();
		const FVector InitialPosition = SourceActor->GetComponentsBoundingBox().GetCenter();
		const FVector InitialScale = FVector(0.5f, 0.5, 1.f);

		FTransform LocalTransform;
		LocalTransform.SetComponents(TargetRotation, InitialPosition, InitialScale);
		FTransform WorldToLocal = LocalTransform.Inverse();

		FBox Bounds(ForceInit);
		for (const UActorComponent* Component : SourceActor->GetComponents())
		{
			// Only gather visual components in the bounds calculation
			const UPrimitiveComponent* PrimitiveComponent = Cast<const UPrimitiveComponent>(Component);
			if (PrimitiveComponent != nullptr && PrimitiveComponent->IsRegistered())
			{
				const FTransform ComponentToActor = PrimitiveComponent->GetComponentTransform() * WorldToLocal;
				FBoxSphereBounds LocalSpaceComponentBounds = PrimitiveComponent->CalcBounds(ComponentToActor);
				Bounds += LocalSpaceComponentBounds.GetBox();
			}
		}

		// Create transform from bounds
		FVector Origin;
		FVector Extent;
		Bounds.GetCenterAndExtents(Origin, Extent);

		Origin = LocalTransform.TransformPosition(Origin);

		FTransform Transform;
		Transform.SetComponents(TargetRotation, Origin, Extent);

		RootComponent->SetWorldTransform(Transform);

		// Update the virtual texture to match the new transform
		UpdateVirtualTexture();
	}
}

#endif
