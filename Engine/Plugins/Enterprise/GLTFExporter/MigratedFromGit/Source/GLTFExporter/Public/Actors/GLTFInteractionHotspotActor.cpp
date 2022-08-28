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
	ForwardPropertiesToComponent();
}
#endif // WITH_EDITOR

void AGLTFInteractionHotspotActor::ForwardPropertiesToComponent()
{
	const bool IsUnequalAnimations = ([&]()
	{
		if (InteractionHotspotComponent->Animations.Num() != Animations.Num())
		{
			return true;
		}

		for (int32 AnimationIndex = 0; AnimationIndex < Animations.Num(); ++AnimationIndex)
		{
			const FGLTFAnimation& ActorAnimation = Animations[AnimationIndex];
			const FGLTFAnimation& ComponentAnimation = InteractionHotspotComponent->Animations[AnimationIndex];

			if (ActorAnimation.SkeletalMeshActor != ComponentAnimation.SkeletalMeshActor || ActorAnimation.AnimationSequence != ComponentAnimation.AnimationSequence)
			{
				return true;
			}
		}

		return false;
	})();

	if (IsUnequalAnimations)
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
