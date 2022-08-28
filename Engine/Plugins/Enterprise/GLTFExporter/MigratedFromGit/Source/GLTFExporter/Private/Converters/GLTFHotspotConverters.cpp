// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFHotspotConverters.h"
#include "Json/FGLTFJsonHotspot.h"
#include "Builders/GLTFContainerBuilder.h"

FGLTFJsonHotspotIndex FGLTFHotspotConverter::Convert(const AGLTFInteractionHotspotActor* HotspotActor)
{
	// TODO: should we warn and / or return INDEX_NONE if the hotspot has no valid Image assigned?

	FGLTFJsonHotspot JsonHotspot;
	HotspotActor->GetName(JsonHotspot.Name);
	JsonHotspot.Animation = FGLTFJsonAnimationIndex(INDEX_NONE);	// TODO: assign the animation property once we have support for exporting animations
	JsonHotspot.Image = Builder.GetOrAddTexture(HotspotActor->Image);
	JsonHotspot.HoveredImage = Builder.GetOrAddTexture(HotspotActor->HoveredImage);
	JsonHotspot.ToggledImage = Builder.GetOrAddTexture(HotspotActor->ToggledImage);
	JsonHotspot.ToggledHoveredImage = Builder.GetOrAddTexture(HotspotActor->ToggledHoveredImage);

	return Builder.AddHotspot(JsonHotspot);
}
