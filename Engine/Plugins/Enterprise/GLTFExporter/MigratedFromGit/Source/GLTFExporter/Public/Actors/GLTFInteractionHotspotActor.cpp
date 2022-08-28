// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/GLTFInteractionHotspotActor.h"
#include "Components/GLTFInteractionHotspotComponent.h"

AGLTFInteractionHotspotActor::AGLTFInteractionHotspotActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// A scene component with a transform in the root
	SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComponent"));
	RootComponent = SceneComponent;

	InteractionHotspotComponent = CreateDefaultSubobject<UGLTFInteractionHotspotComponent>(TEXT("InteractionHotspotComponent"));
	InteractionHotspotComponent->SetupAttachment(SceneComponent);

	OnBeginCursorOver.AddDynamic(this, &AGLTFInteractionHotspotActor::BeginCursorOver);
}

void AGLTFInteractionHotspotActor::BeginPlay()
{
	Super::BeginPlay();
}

void AGLTFInteractionHotspotActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AGLTFInteractionHotspotActor::BeginCursorOver(AActor* TouchedActor)
{
	UE_LOG(LogTemp, Warning, TEXT("AGLTFInteractionHotspotActor::BeginCursorOver()"));
}
