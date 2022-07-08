// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlate.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MediaPlateComponent.h"
#include "MediaPlateModule.h"
#include "MediaTexture.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MediaPlate"

FLazyName AMediaPlate::MediaPlateComponentName(TEXT("MediaPlateComponent0"));
FLazyName AMediaPlate::MediaTextureName("MediaTexture");

AMediaPlate::AMediaPlate(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// Set up media component.
	MediaPlateComponent = CreateDefaultSubobject<UMediaPlateComponent>(MediaPlateComponentName);

	// Set up static mesh component.
	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMeshComponent"));
	StaticMeshComponent->SetupAttachment(RootComponent);
	MediaPlateComponent->StaticMeshComponent = StaticMeshComponent;

	// Hook up mesh.
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UStaticMesh> Plane;
		FConstructorStatics()
			: Plane(TEXT("/MediaPlate/SM_MediaPlateScreen"))
		{}
	};

	static FConstructorStatics ConstructorStatics;
	if (ConstructorStatics.Plane.Object != nullptr)
	{
		StaticMeshComponent->SetStaticMesh(ConstructorStatics.Plane.Object);
	}
}

void AMediaPlate::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	if (MediaPlateComponent != nullptr)
	{
		if (StaticMeshComponent != nullptr)
		{
			UMaterialInstanceDynamic* MediaMID = nullptr;

			// Do we have a material?
			if (StaticMeshComponent->GetNumOverrideMaterials() > 0)
			{
				MediaMID = StaticMeshComponent->CreateAndSetMaterialInstanceDynamicFromMaterial(0, StaticMeshComponent->GetMaterial(0));
			}
			else
			{
				// Add default material.
				UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/MediaPlate/M_MediaPlate"));
				if (Material != nullptr)
				{
					MediaMID = StaticMeshComponent->CreateAndSetMaterialInstanceDynamicFromMaterial(0, Material);
				}
			}

			// Set up the material to point to our media texture.
			if (MediaMID != nullptr)
			{
				MediaMID->SetTextureParameterValue(MediaTextureName, MediaPlateComponent->GetMediaTexture());
			}
		}

		// Add our media texture to the tracker.
		MediaPlateComponent->RegisterWithMediaTextureTracker();
	}
}

void AMediaPlate::BeginDestroy()
{
	if (MediaPlateComponent != nullptr)
	{
		// Remove our media texture.
		MediaPlateComponent->UnregisterWithMediaTextureTracker();
	}
	
	Super::BeginDestroy();
}

#undef LOCTEXT_NAMESPACE
