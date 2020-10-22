// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterCameraComponent.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "UObject/ConstructorHelpers.h"

#include "DisplayClusterConfigurationTypes.h"


UDisplayClusterCameraComponent::UDisplayClusterCameraComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, InterpupillaryDistance(6.4f)
	, bSwapEyes(false)
	, StereoOffset(EDisplayClusterEyeStereoOffset::None)
{
	// Children of UDisplayClusterSceneComponent must always Tick to be able to process VRPN tracking
	PrimaryComponentTick.bCanEverTick = true;

#if WITH_EDITOR
	if (GIsEditor)
	{
		// Create visual mesh component as a child
		VisCameraComponent = CreateDefaultSubobject<UStaticMeshComponent>(FName(*(GetName() + FString("_impl"))));
		if (VisCameraComponent)
		{
			static ConstructorHelpers::FObjectFinder<UStaticMesh> ScreenMesh(TEXT("/Engine/EditorMeshes/Camera/SM_CineCam"));

			VisCameraComponent->SetFlags(EObjectFlags::RF_DuplicateTransient | RF_Transient | RF_TextExportTransient);
			VisCameraComponent->AttachToComponent(this, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
			VisCameraComponent->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator(0.f, 90.f, 0.f));
			VisCameraComponent->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.5f));
			VisCameraComponent->SetStaticMesh(ScreenMesh.Object);
			VisCameraComponent->SetMobility(EComponentMobility::Movable);
			VisCameraComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			VisCameraComponent->SetVisibility(true);
		}
	}
#endif
}

void UDisplayClusterCameraComponent::ApplyConfigurationData()
{
	Super::ApplyConfigurationData();

	const UDisplayClusterConfigurationSceneComponentCamera* CfgCamera = Cast<UDisplayClusterConfigurationSceneComponentCamera>(GetConfigParameters());
	if (CfgCamera)
	{
		InterpupillaryDistance = CfgCamera->InterpupillaryDistance;
		bSwapEyes = CfgCamera->bSwapEyes;

		switch (CfgCamera->StereoOffset)
		{
		case EDisplayClusterConfigurationEyeStereoOffset::Left:
			StereoOffset = EDisplayClusterEyeStereoOffset::Left;
			break;

		case EDisplayClusterConfigurationEyeStereoOffset::None:
			StereoOffset = EDisplayClusterEyeStereoOffset::None;
			break;

		case EDisplayClusterConfigurationEyeStereoOffset::Right:
			StereoOffset = EDisplayClusterEyeStereoOffset::Right;
			break;

		default:
			StereoOffset = EDisplayClusterEyeStereoOffset::None;
			break;
		}
	}
}

#if WITH_EDITOR
void UDisplayClusterCameraComponent::SetNodeSelection(bool bSelect)
{
	VisCameraComponent->bDisplayVertexColors = bSelect;
	VisCameraComponent->PushSelectionToProxy();
}
#endif
