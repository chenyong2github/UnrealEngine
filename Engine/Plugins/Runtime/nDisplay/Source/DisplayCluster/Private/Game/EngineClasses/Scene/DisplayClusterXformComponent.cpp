// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterXformComponent.h"

#include "DisplayClusterRootActor.h"
#include "Components/StaticMeshComponent.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "UObject/ConstructorHelpers.h"


UDisplayClusterXformComponent::UDisplayClusterXformComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		// Create visual mesh component as a child
		const FName ImplName = FName(*(GetName() + FString("_impl")));
		
		VisXformComponent = CreateDefaultSubobject<UStaticMeshComponent>(ImplName);
		if (VisXformComponent)
		{
			static ConstructorHelpers::FObjectFinder<UStaticMesh> ScreenMesh(TEXT("/nDisplay/Meshes/sm_nDisplayXform"));

			VisXformComponent->AttachToComponent(this, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
			VisXformComponent->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
			VisXformComponent->SetRelativeScale3D(FVector::OneVector);
			VisXformComponent->SetStaticMesh(ScreenMesh.Object);
			VisXformComponent->SetMobility(EComponentMobility::Movable);
			VisXformComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			VisXformComponent->SetVisibility(true);
			VisXformComponent->SetIsVisualizationComponent(true);
		}
	}
#endif
}

void UDisplayClusterXformComponent::PostInitProperties()
{
	Super::PostInitProperties();

#if WITH_EDITOR
	ADisplayClusterRootActor* RootActor = Cast<ADisplayClusterRootActor>(GetOuter());
	float Scale = RootActor ? RootActor->GetXformGizmoScale() : 1;
	bool bIsVisible = RootActor ? RootActor->GetXformGizmoVisibility() : true;

	SetVisXformScale(Scale);
	SetVisXformVisibility(bIsVisible);
#endif
}

#if WITH_EDITOR
void UDisplayClusterXformComponent::SetVisXformScale(float InScale)
{
	if (VisXformComponent)
	{
		VisXformComponent->SetRelativeScale3D(FVector(InScale));
	}
}

void UDisplayClusterXformComponent::SetVisXformVisibility(bool bIsVisible)
{
	if (VisXformComponent)
	{
		VisXformComponent->SetVisibility(bIsVisible);
	}
}

void UDisplayClusterXformComponent::SetNodeSelection(bool bSelect)
{
	VisXformComponent->bDisplayVertexColors = bSelect;
	VisXformComponent->PushSelectionToProxy();
}
#endif
