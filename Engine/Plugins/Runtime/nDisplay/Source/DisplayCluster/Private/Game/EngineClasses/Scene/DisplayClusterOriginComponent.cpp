// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterOriginComponent.h"
#include "Components/StaticMeshComponent.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "UObject/ConstructorHelpers.h"


UDisplayClusterOriginComponent::UDisplayClusterOriginComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;

#if WITH_EDITOR
	if (GIsEditor)
	{
		// Create visual mesh component as a child
		VisualizationComponent = CreateDefaultSubobject<UStaticMeshComponent>(FName(*(GetName() + FString("_impl"))));
		if (VisualizationComponent)
		{
			static ConstructorHelpers::FObjectFinder<UStaticMesh> OriginMesh(TEXT("/Engine/BasicShapes/Sphere"));
			static ConstructorHelpers::FObjectFinder<UMaterial>   OriginMaterial(TEXT("/Engine/EditorMaterials/WidgetMaterial_Z"));

			VisualizationComponent->SetFlags(EObjectFlags::RF_DuplicateTransient | RF_Transient | RF_TextExportTransient);
			VisualizationComponent->AttachToComponent(this, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
			VisualizationComponent->SetMaterial(0, OriginMaterial.Object);
			VisualizationComponent->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator, false);
			VisualizationComponent->SetRelativeScale3D(FVector(0.1f, 0.1f, 0.1f));
			VisualizationComponent->SetStaticMesh(OriginMesh.Object);
			VisualizationComponent->SetMobility(EComponentMobility::Movable);
			VisualizationComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			VisualizationComponent->SetVisibility(true);
		}
	}
#endif
}
