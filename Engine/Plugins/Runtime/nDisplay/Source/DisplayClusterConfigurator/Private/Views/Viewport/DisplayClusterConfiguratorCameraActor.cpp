// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Viewport/DisplayClusterConfiguratorCameraActor.h"

#include "DisplayClusterConfigurationTypes.h"

#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"


ADisplayClusterConfiguratorCameraActor::ADisplayClusterConfiguratorCameraActor()
	: bSelected(false)
{
}

void ADisplayClusterConfiguratorCameraActor::AddComponents()
{
	// Add Camera
	UStaticMesh* CineCamMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/EditorMeshes/Camera/SM_CineCam.SM_CineCam"), nullptr, LOAD_None, nullptr);
	UDisplayClusterConfigurationSceneComponentCamera* Camera = CastChecked<UDisplayClusterConfigurationSceneComponentCamera>(ObjectToEdit.Get());

	USceneComponent* NewSceneComp = NewObject<USceneComponent>(this, USceneComponent::StaticClass(), "NewSceneComp", RF_NoFlags);
	CineCamComponent = NewObject<UStaticMeshComponent>(this, UStaticMeshComponent::StaticClass(), "CineCamComponent", RF_NoFlags);

	RootComponent = NewSceneComp;

	const FQuat NewWorldRotation = FRotator(0.f, 90.f, 0.f).Quaternion() * Camera->Rotation.Quaternion();
	FTransform Transform(NewWorldRotation, Camera->Location, FVector(1.0f));
	CineCamComponent->SetupAttachment(NewSceneComp);
	CineCamComponent->SetStaticMesh(CineCamMesh);
	CineCamComponent->SetRelativeTransform(Transform);
	CineCamComponent->RegisterComponentWithWorld(GetWorld());
}

void ADisplayClusterConfiguratorCameraActor::SetColor(const FColor& Color)
{
}

void ADisplayClusterConfiguratorCameraActor::SetNodeSelection(bool bSelect)
{
	if (bSelect)
	{
		if (bSelected == false)
		{
			bSelected = true;
			OnSelection();
		}
	}
	else
	{
		bSelected = false;
	}

	CineCamComponent->bDisplayVertexColors = bSelect;
	CineCamComponent->PushSelectionToProxy();
}
