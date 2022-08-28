// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFInteractionHotspotConverters.h"
#include "Json/FGLTFJsonInteractionHotspot.h"
#include "Builders/GLTFContainerBuilder.h"

FGLTFJsonInteractionHotspotIndex FGLTFInteractionHotspotComponentConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const UGLTFInteractionHotspotComponent* HotspotComponent)
{
	// TODO: should we warn and / or return INDEX_NONE if the hotspot has no valid Image assigned?

	FGLTFJsonInteractionHotspot JsonHotspot;
	JsonHotspot.Name = Name;
	JsonHotspot.Animation = FGLTFJsonAnimationIndex(INDEX_NONE);	// TODO: assign the animation property once we have support for exporting animations
	JsonHotspot.Image = Builder.GetOrAddTexture(HotspotComponent->Image);
	JsonHotspot.HoveredImage = Builder.GetOrAddTexture(HotspotComponent->HoveredImage);
	JsonHotspot.ToggledImage = Builder.GetOrAddTexture(HotspotComponent->ToggledImage);
	JsonHotspot.ToggledHoveredImage = Builder.GetOrAddTexture(HotspotComponent->ToggledHoveredImage);

	return Builder.AddInteractionHotspot(JsonHotspot);
}
