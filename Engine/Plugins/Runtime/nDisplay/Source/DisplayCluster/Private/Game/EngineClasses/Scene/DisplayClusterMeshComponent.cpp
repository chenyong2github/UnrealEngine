// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterMeshComponent.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "UObject/ConstructorHelpers.h"

#include "DisplayClusterConfigurationTypes.h"


UDisplayClusterMeshComponent::UDisplayClusterMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Create visual mesh component as a child
	WarpMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(FName(*(GetName() + FString("_impl"))));
	if (WarpMeshComponent)
	{
#if !WITH_EDITOR
		WarpMeshComponent->SetFlags(EObjectFlags::RF_DuplicateTransient | RF_Transient | RF_TextExportTransient);
		WarpMeshComponent->SetVisibility(false);
#endif

		WarpMeshComponent->AttachToComponent(this, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
		WarpMeshComponent->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
		WarpMeshComponent->SetRelativeScale3D(FVector::OneVector);
		WarpMeshComponent->SetMobility(EComponentMobility::Movable);
		WarpMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

#if WITH_EDITOR
		WarpMeshComponent->SetVisibility(true);
		WarpMeshComponent->SetIsVisualizationComponent(true);
#endif /*WITH_EDITOR*/
	}
}

void UDisplayClusterMeshComponent::ApplyConfigurationData()
{
	Super::ApplyConfigurationData();

	const UDisplayClusterConfigurationSceneComponentMesh* CfgMesh = Cast<UDisplayClusterConfigurationSceneComponentMesh>(GetConfigParameters());
	if (CfgMesh && !CfgMesh->AssetPath.IsEmpty())
	{
		//@todo Load and assign mesh asset
	}
}

#if WITH_EDITOR
void UDisplayClusterMeshComponent::SetNodeSelection(bool bSelect)
{
	WarpMeshComponent->bDisplayVertexColors = bSelect;
	WarpMeshComponent->PushSelectionToProxy();
}
#endif
