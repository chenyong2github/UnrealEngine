// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/GLTFInteractionHotspotActor.h"

AGLTFInteractionHotspotActor::AGLTFInteractionHotspotActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	DefaultSprite(nullptr),
	HighlightSprite(nullptr),
	ToggledSprite(nullptr)
{
	// A scene component with a transform in the root
	SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComponent"));
	RootComponent = SceneComponent;

	InteractionHotspotComponent = CreateDefaultSubobject<UGLTFInteractionHotspotComponent>(TEXT("InteractionHotspotComponent"));
	InteractionHotspotComponent->SetupAttachment(SceneComponent);
	ForwardPropertiesToComponent();
}

#if WITH_EDITOR
void AGLTFInteractionHotspotActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	ForwardPropertiesToComponent();
}
#endif // WITH_EDITOR

void AGLTFInteractionHotspotActor::ForwardPropertiesToComponent()
{
	if (InteractionHotspotComponent->Animations != Animations)
	{
		InteractionHotspotComponent->Animations = Animations;
	}
	
	if (InteractionHotspotComponent->DefaultSprite != DefaultSprite)
	{
		InteractionHotspotComponent->DefaultSprite = DefaultSprite;
		InteractionHotspotComponent->SetSprite(DefaultSprite);
	}
	
	if (InteractionHotspotComponent->HighlightSprite != HighlightSprite)
	{
		InteractionHotspotComponent->HighlightSprite = HighlightSprite;
	}
	
	if (InteractionHotspotComponent->ToggledSprite != ToggledSprite)
	{
		InteractionHotspotComponent->ToggledSprite = ToggledSprite;
	}
}
