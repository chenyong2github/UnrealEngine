// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFHotspotConverters.h"
#include "Json/FGLTFJsonHotspot.h"
#include "Builders/GLTFContainerBuilder.h"

FGLTFJsonHotspotIndex FGLTFHotspotComponentConverter::Convert(const UGLTFInteractionHotspotComponent* HotspotComponent)
{
	// TODO: should we warn and / or return INDEX_NONE if the hotspot has no valid Image assigned?

	FGLTFJsonHotspot JsonHotspot;
	HotspotComponent->GetName(JsonHotspot.Name);
	JsonHotspot.Animation = FGLTFJsonAnimationIndex(INDEX_NONE);	// TODO: assign the animation property once we have support for exporting animations
	JsonHotspot.Image = Builder.GetOrAddTexture(HotspotComponent->Image);
	JsonHotspot.HoveredImage = Builder.GetOrAddTexture(HotspotComponent->HoveredImage);
	JsonHotspot.ToggledImage = Builder.GetOrAddTexture(HotspotComponent->ToggledImage);
	JsonHotspot.ToggledHoveredImage = Builder.GetOrAddTexture(HotspotComponent->ToggledHoveredImage);

	return Builder.AddHotspot(JsonHotspot);
}
