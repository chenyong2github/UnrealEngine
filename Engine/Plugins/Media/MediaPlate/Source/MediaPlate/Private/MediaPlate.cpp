// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlate.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "MediaComponent.h"
#include "MediaPlayer.h"
#include "MediaPlateModule.h"
#include "MediaTexture.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MediaPlate"

FLazyName AMediaPlate::MediaComponentName(TEXT("MediaComponent0"));
FLazyName AMediaPlate::MediaTextureName("MediaTexture");

AMediaPlate::AMediaPlate(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MediaComponent = CreateDefaultSubobject<UMediaComponent>(MediaComponentName);

	// Hook up mesh.
	UStaticMeshComponent* LocalStaticMeshComponent = GetStaticMeshComponent();
	check(LocalStaticMeshComponent);
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UStaticMesh> Plane;
		FConstructorStatics()
			: Plane(TEXT("/Engine/BasicShapes/Plane"))
		{}
	};

	static FConstructorStatics ConstructorStatics;
	if (ConstructorStatics.Plane.Object != nullptr)
	{
		LocalStaticMeshComponent->SetStaticMesh(ConstructorStatics.Plane.Object);
	}
}

void AMediaPlate::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	UStaticMeshComponent* LocalStaticMeshComponent = GetStaticMeshComponent();
	check(LocalStaticMeshComponent);

	// Add material.
	UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/MediaPlate/M_MediaPlate"));
	if (Material != nullptr)
	{
		UMaterialInstanceDynamic* MaterialInstance = LocalStaticMeshComponent->CreateAndSetMaterialInstanceDynamicFromMaterial(0, Material);
		if (MaterialInstance != nullptr)
		{
			MaterialInstance->SetTextureParameterValue(MediaTextureName, MediaComponent->GetMediaTexture());
		}
	}
}

#undef LOCTEXT_NAMESPACE
