// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterScreenComponent.h"
#include "Components/StaticMeshComponent.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "UObject/ConstructorHelpers.h"

#include "DisplayClusterRootActor.h"
#include "DisplayClusterConfigurationTypes.h"

#include "Game/IPDisplayClusterGameManager.h"
#include "Misc/DisplayClusterGlobals.h"


UDisplayClusterScreenComponent::UDisplayClusterScreenComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Children of UDisplayClusterSceneComponent must always Tick to be able to process VRPN tracking
	PrimaryComponentTick.bCanEverTick = true;

#if WITH_EDITOR
	if (GIsEditor)
	{
		// Create visual mesh component as a child
		VisScreenComponent = CreateDefaultSubobject<UStaticMeshComponent>(FName(*(GetName() + FString("_impl"))));
		if (VisScreenComponent)
		{
			static ConstructorHelpers::FObjectFinder<UStaticMesh> ScreenMesh(TEXT("/nDisplay/Meshes/plane_1x1"));

			VisScreenComponent->SetFlags(EObjectFlags::RF_DuplicateTransient | RF_Transient | RF_TextExportTransient);
			VisScreenComponent->AttachToComponent(this, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
			VisScreenComponent->RegisterComponentWithWorld(GetWorld());

			VisScreenComponent->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
			VisScreenComponent->SetRelativeScale3D(FVector::OneVector);
			VisScreenComponent->SetStaticMesh(ScreenMesh.Object);
			VisScreenComponent->SetMobility(EComponentMobility::Movable);
			VisScreenComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			VisScreenComponent->SetVisibility(true);
		}
	}
#endif
}

void UDisplayClusterScreenComponent::ApplyConfigurationData()
{
	Super::ApplyConfigurationData();

	const UDisplayClusterConfigurationSceneComponentScreen* CfgScreen = Cast<UDisplayClusterConfigurationSceneComponentScreen>(GetConfigParameters());
	if (CfgScreen)
	{
		SetScreenSize(CfgScreen->Size);
	}
}

FVector2D UDisplayClusterScreenComponent::GetScreenSize() const
{
	return Size;
}

void UDisplayClusterScreenComponent::SetScreenSize(const FVector2D& InSize)
{
	Size = InSize;

#if WITH_EDITOR
	if (VisScreenComponent)
	{
		VisScreenComponent->SetRelativeScale3D(FVector(1.f, Size.X, Size.Y));
	}
#endif
}

#if WITH_EDITOR
void UDisplayClusterScreenComponent::SetNodeSelection(bool bSelect)
{
	if (VisScreenComponent)
	{
		VisScreenComponent->bDisplayVertexColors = bSelect;
		VisScreenComponent->PushSelectionToProxy();
	}
}
#endif
