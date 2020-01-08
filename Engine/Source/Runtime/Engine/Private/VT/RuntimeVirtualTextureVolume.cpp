// Copyright Epic Games, Inc. All Rights Reserved.

#include "VT/RuntimeVirtualTextureVolume.h"

#include "Components/BoxComponent.h"
#include "Components/RuntimeVirtualTextureComponent.h"

ARuntimeVirtualTextureVolume::ARuntimeVirtualTextureVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RootComponent = VirtualTextureComponent = CreateDefaultSubobject<URuntimeVirtualTextureComponent>(TEXT("VirtualTextureComponent"));

#if WITH_EDITORONLY_DATA
	// Add box for visualization of bounds
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
	Box->SetBoxExtent(FVector(0.5f, 0.5f, 1.f), false);
	Box->SetIsVisualizationComponent(true);
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->SetCanEverAffectNavigation(false);
	Box->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;
	Box->SetGenerateOverlapEvents(false);
	Box->SetupAttachment(VirtualTextureComponent);
#endif
}
