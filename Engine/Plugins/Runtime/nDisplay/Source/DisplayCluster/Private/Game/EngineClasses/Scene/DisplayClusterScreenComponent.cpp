// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterScreenComponent.h"
#include "Components/StaticMeshComponent.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "UObject/ConstructorHelpers.h"

#include "DisplayClusterRootActor.h"
#include "DisplayClusterConfigurationTypes.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "Game/IPDisplayClusterGameManager.h"
#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterStrings.h"


UDisplayClusterScreenComponent::UDisplayClusterScreenComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Children of UDisplayClusterSceneComponent must always Tick to be able to process VRPN tracking
	PrimaryComponentTick.bCanEverTick = true;

	// Create visual mesh component as a child
	ScreenGeometryComponent = CreateDefaultSubobject<UStaticMeshComponent>(FName(*(GetName() + FString("_impl"))));
	if (ScreenGeometryComponent)
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> ScreenMesh(TEXT("StaticMesh'/Engine/BasicShapes/Cube.Cube'"));

		ScreenGeometryComponent->AttachToComponent(this, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
		ScreenGeometryComponent->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator, false);
		ScreenGeometryComponent->SetStaticMesh(ScreenMesh.Object);
		ScreenGeometryComponent->SetMobility(EComponentMobility::Movable);
		ScreenGeometryComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ScreenGeometryComponent->SetVisibility(false);
	}
}


void UDisplayClusterScreenComponent::BeginPlay()
{
	Super::BeginPlay();

	const UDisplayClusterConfigurationSceneComponentScreen* CfgScreen = Cast<UDisplayClusterConfigurationSceneComponentScreen>(GetConfigParameters());
	if (CfgScreen)
	{
		Size = CfgScreen->Size;
	}

	// Register visual mesh component
	if (ScreenGeometryComponent)
	{
		ScreenGeometryComponent->RegisterComponent();
	}
}

void UDisplayClusterScreenComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void UDisplayClusterScreenComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// Update projection screen material
	static const IPDisplayClusterGameManager* const GameMgr = GDisplayCluster->GetPrivateGameMgr();
	if (GameMgr)
	{
		ADisplayClusterRootActor* const RootActor = GameMgr->GetRootActor();
		if (RootActor)
		{
			if (RootActor->GetProjectionScreenMaterial())
			{
				ScreenGeometryComponent->SetMaterial(0, RootActor->GetProjectionScreenMaterial());
			}

			const bool bScreenVisible = RootActor->GetShowProjectionScreens();
			ScreenGeometryComponent->SetVisibility(bScreenVisible);
		}
	}

	// Update screen size
	SetRelativeScale3D(FVector(0.005f, Size.X / 100.f, Size.Y / 100.f));

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}
