// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/GLTFInteractionHotspotActor.h"
#include "Components/GLTFInteractionHotspotComponent.h"

AGLTFInteractionHotspotActor::AGLTFInteractionHotspotActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	SkeletalMeshActor(nullptr),
	AnimationSequence(nullptr),
	DefaultSprite(nullptr),
	HighlightSprite(nullptr),
	ClickSprite(nullptr),
	Radius(50.0f)
{
	// A scene component with a transform in the root
	SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComponent"));
	RootComponent = SceneComponent;

	InteractionHotspotComponent = CreateDefaultSubobject<UGLTFInteractionHotspotComponent>(TEXT("InteractionHotspotComponent"));
	InteractionHotspotComponent->SetupAttachment(SceneComponent);
	ForwardPropertiesToComponent();
}

void AGLTFInteractionHotspotActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	ForwardPropertiesToComponent();
}

void AGLTFInteractionHotspotActor::ForwardPropertiesToComponent()
{
	if (InteractionHotspotComponent->SkeletalMeshActor != SkeletalMeshActor)
	{
		InteractionHotspotComponent->SkeletalMeshActor = SkeletalMeshActor;
	}
	else if (InteractionHotspotComponent->AnimationSequence != AnimationSequence)
	{
		InteractionHotspotComponent->AnimationSequence = AnimationSequence;
	}
	else if (InteractionHotspotComponent->DefaultSprite != DefaultSprite)
	{
		InteractionHotspotComponent->DefaultSprite = DefaultSprite;
	}
	else if (InteractionHotspotComponent->HighlightSprite != HighlightSprite)
	{
		InteractionHotspotComponent->HighlightSprite = HighlightSprite;
	}
	else if (InteractionHotspotComponent->ClickSprite != ClickSprite)
	{
		InteractionHotspotComponent->ClickSprite = ClickSprite;
	}
	else if (InteractionHotspotComponent->Radius != Radius)
	{
		InteractionHotspotComponent->Radius = Radius;
	}
}
