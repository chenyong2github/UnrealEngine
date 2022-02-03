// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlate.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "MediaComponent.h"
#include "MediaPlayer.h"
#include "MediaPlateModule.h"
#include "MediaTexture.h"

#define LOCTEXT_NAMESPACE "MediaPlate"

FLazyName AMediaPlate::MediaComponentName(TEXT("MediaComponent0"));

AMediaPlate::AMediaPlate(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MediaComponent = CreateDefaultSubobject<UMediaComponent>(MediaComponentName);
}

void AMediaPlate::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	UStaticMeshComponent* LocalStaticMeshComponent = GetStaticMeshComponent();
	check(LocalStaticMeshComponent);

	// Add mesh.
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane"));
	if (Mesh != nullptr)
	{
		LocalStaticMeshComponent->SetStaticMesh(Mesh);
	}
}

#undef LOCTEXT_NAMESPACE
