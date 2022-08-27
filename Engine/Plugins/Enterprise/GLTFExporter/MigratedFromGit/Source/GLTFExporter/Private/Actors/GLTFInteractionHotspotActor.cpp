// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/GLTFInteractionHotspotActor.h"

AGLTFInteractionHotspotActor::AGLTFInteractionHotspotActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InteractionHotspotComponent = CreateDefaultSubobject<UGLTFInteractionHotspotComponent>(TEXT("InteractionHotspotComponent"));
	RootComponent = InteractionHotspotComponent;
}
