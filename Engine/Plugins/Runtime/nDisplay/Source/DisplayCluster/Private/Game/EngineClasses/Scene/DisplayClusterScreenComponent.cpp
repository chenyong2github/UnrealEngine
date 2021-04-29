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

#define SIZE_TO_CM(InSize) \
	InSize * 100.f

#define SIZE_FROM_CM(InSize) \
	InSize / 100.f

UDisplayClusterScreenComponent::UDisplayClusterScreenComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Size = SIZE_FROM_CM(FVector2D(100.f, 56.25f));

#if WITH_EDITORONLY_DATA
	SizeCm = SIZE_TO_CM(Size);
#endif
	
#if WITH_EDITOR
	if (GIsEditor)
	{
		const FName VisName = FName(*(GetName() + FString("_impl")));
		// Create visual mesh component as a child
		
		static ConstructorHelpers::FObjectFinder<UStaticMesh> ScreenMesh(TEXT("/nDisplay/Meshes/plane_1x1"));
		VisScreenComponent = CreateDefaultSubobject<UStaticMeshComponent>(VisName);
		VisScreenComponent->SetFlags(RF_Public);
		
		VisScreenComponent->AttachToComponent(this, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
		VisScreenComponent->RegisterComponentWithWorld(GetWorld());

		VisScreenComponent->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
		VisScreenComponent->SetRelativeScale3D(FVector::OneVector);
		VisScreenComponent->SetStaticMesh(ScreenMesh.Object);
		VisScreenComponent->SetMobility(EComponentMobility::Movable);
		VisScreenComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		VisScreenComponent->SetVisibility(true);
		VisScreenComponent->SetIsVisualizationComponent(true);
	}
#endif
}

void UDisplayClusterScreenComponent::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	SizeCm = SIZE_TO_CM(Size);
#endif
}

#if WITH_EDITOR
void UDisplayClusterScreenComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.MemberProperty &&
		PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UDisplayClusterScreenComponent, SizeCm))
	{
		Size = SIZE_FROM_CM(SizeCm);
		SetScreenSize(Size);
	}
}
#endif

void UDisplayClusterScreenComponent::ApplyConfigurationData()
{
	Super::ApplyConfigurationData();

	if (DoesComponentBelongToBlueprint())
	{
		/*
			Blueprint already contains component information, position, and heirarchy.
			When this isn't a blueprint (such as config data only or on initial import) we can apply config data.
		*/
		SetScreenSize(Size);
	}
	else if (const UDisplayClusterConfigurationSceneComponentScreen* CfgScreen = Cast<UDisplayClusterConfigurationSceneComponentScreen>(GetConfigParameters()))
	{
		SetScreenSize(CfgScreen->Size);
	}
}

FVector2D UDisplayClusterScreenComponent::GetScreenSizeScaled() const
{
	const FVector ComponentScale = GetComponentScale();
	const FVector2D ComponentScale2D(ComponentScale.Y, ComponentScale.Z);
	return GetScreenSize() * ComponentScale2D;
}

FVector2D UDisplayClusterScreenComponent::GetScreenSize() const
{
	return Size;
}

void UDisplayClusterScreenComponent::SetScreenSize(const FVector2D& InSize)
{
	Size = InSize;

#if WITH_EDITORONLY_DATA
	SizeCm = SIZE_TO_CM(Size);
#endif
	
#if WITH_EDITOR
	if (VisScreenComponent)
	{
		VisScreenComponent->SetRelativeScale3D(FVector(1.f, Size.X * 100.f, Size.Y * 100.f));
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
