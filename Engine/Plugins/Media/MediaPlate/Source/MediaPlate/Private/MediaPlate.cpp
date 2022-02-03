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

}

#undef LOCTEXT_NAMESPACE
