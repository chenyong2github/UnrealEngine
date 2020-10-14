// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterXformComponent.h"

#include "Components/StaticMeshComponent.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "UObject/ConstructorHelpers.h"


UDisplayClusterXformComponent::UDisplayClusterXformComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Children of UDisplayClusterSceneComponent must always Tick to be able to process VRPN tracking
	PrimaryComponentTick.bCanEverTick = true;

#if WITH_EDITOR
	if (GIsEditor)
	{
		// Create visual mesh component as a child
		VisXformComponent = CreateDefaultSubobject<UStaticMeshComponent>(FName(*(GetName() + FString("_impl"))));
		if (VisXformComponent)
		{
			static ConstructorHelpers::FObjectFinder<UStaticMesh> ScreenMesh(TEXT("/Engine/VREditor/TransformGizmo/SM_Sequencer_Node"));

			VisXformComponent->SetFlags(EObjectFlags::RF_DuplicateTransient | RF_Transient | RF_TextExportTransient);
			VisXformComponent->AttachToComponent(this, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
			VisXformComponent->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
			VisXformComponent->SetRelativeScale3D(FVector::OneVector);
			VisXformComponent->SetStaticMesh(ScreenMesh.Object);
			VisXformComponent->SetMobility(EComponentMobility::Movable);
			VisXformComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			VisXformComponent->SetVisibility(true);
		}
	}
#endif
}

#if WITH_EDITOR
void UDisplayClusterXformComponent::SetNodeSelection(bool bSelect)
{
	VisXformComponent->bDisplayVertexColors = bSelect;
	VisXformComponent->PushSelectionToProxy();
}
#endif
