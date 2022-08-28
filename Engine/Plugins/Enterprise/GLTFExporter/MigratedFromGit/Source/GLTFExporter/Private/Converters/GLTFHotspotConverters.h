// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Actors/GLTFInteractionHotspotActor.h"

class FGLTFHotspotConverter final : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonHotspotIndex, const AGLTFInteractionHotspotActor*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonHotspotIndex Convert(const AGLTFInteractionHotspotActor* HotspotActor) override;
};
