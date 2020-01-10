// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterScreenComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameEngine.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "UObject/ConstructorHelpers.h"

#include "DisplayClusterRootActor.h"

#include "Game/IPDisplayClusterGameManager.h"
#include "DisplayClusterGlobals.h"
#include "EngineDefines.h"


UDisplayClusterScreenComponent::UDisplayClusterScreenComponent(const FObjectInitializer& ObjectInitializer) :
	UDisplayClusterSceneComponent(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;

	ScreenGeometryComponent = CreateDefaultSubobject<UStaticMeshComponent>(FName(*(GetName() + FString("_impl"))));
	check(ScreenGeometryComponent);

	if (ScreenGeometryComponent)
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> ScreenMesh(TEXT("StaticMesh'/Engine/BasicShapes/Cube.Cube'"));

		ScreenGeometryComponent->AttachToComponent(this, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
		ScreenGeometryComponent->SetStaticMesh(ScreenMesh.Object);
		ScreenGeometryComponent->SetMobility(EComponentMobility::Movable);
		ScreenGeometryComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ScreenGeometryComponent->SetVisibility(false);
	}
}


void UDisplayClusterScreenComponent::BeginPlay()
{
	Super::BeginPlay();
}


void UDisplayClusterScreenComponent::TickComponent( float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction )
{
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

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UDisplayClusterScreenComponent::SetSettings(const FDisplayClusterConfigSceneNode* pConfig)
{
	const FDisplayClusterConfigScreen* pScreenCfg = static_cast<const FDisplayClusterConfigScreen*>(pConfig);
	Size = pScreenCfg->Size;

	Super::SetSettings(pConfig);
}

bool UDisplayClusterScreenComponent::ApplySettings()
{
	Super::ApplySettings();

#if WITH_EDITOR
	if (ScreenGeometryComponent)
	{
		ScreenGeometryComponent->RegisterComponent();
		ScreenGeometryComponent->AttachToComponent(this, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
		ScreenGeometryComponent->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator, false);
	}
#endif

	SetRelativeScale3D(FVector(0.0001f, Size.X, Size.Y));

	return true;
}
