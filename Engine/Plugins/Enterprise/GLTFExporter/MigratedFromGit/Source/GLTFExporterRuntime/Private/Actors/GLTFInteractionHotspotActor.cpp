// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/GLTFInteractionHotspotActor.h"

AGLTFInteractionHotspotActor::AGLTFInteractionHotspotActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// TODO: Despite the HideCategory class specifier present in both the hotspot actor and the component, the category "Sprite" bleeds through from UBillboardComponent
	InteractionHotspotComponent = CreateDefaultSubobject<UGLTFInteractionHotspotComponent>(TEXT("InteractionHotspotComponent"));
	RootComponent = InteractionHotspotComponent;
}
