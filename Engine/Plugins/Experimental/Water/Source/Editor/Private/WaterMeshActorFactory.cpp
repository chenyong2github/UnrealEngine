// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterMeshActorFactory.h"
#include "WaterMeshActor.h"
#include "WaterMeshComponent.h"
#include "WaterEditorSettings.h"

#define LOCTEXT_NAMESPACE "WaterMeshActorFactory"

UWaterMeshActorFactory::UWaterMeshActorFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DisplayName = LOCTEXT("WaterMeshActorDisplayName", "Water Mesh");
	NewActorClass = AWaterMeshActor::StaticClass();
	bUseSurfaceOrientation = true;
}

void UWaterMeshActorFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);
	
	AWaterMeshActor* WaterMesh = CastChecked<AWaterMeshActor>(NewActor);

	const FWaterMeshActorDefaults& WaterMeshActorDefaults = GetDefault<UWaterEditorSettings>()->WaterMeshActorDefaults;
	WaterMesh->GetWaterMeshComponent()->FarDistanceMaterial = WaterMeshActorDefaults.GetFarDistanceMaterial();
	WaterMesh->GetWaterMeshComponent()->FarDistanceMeshExtent = WaterMeshActorDefaults.FarDistanceMeshExtent;
}

#undef LOCTEXT_NAMESPACE