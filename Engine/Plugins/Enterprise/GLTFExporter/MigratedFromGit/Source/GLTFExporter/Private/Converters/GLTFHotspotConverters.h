// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Actors/GLTFHotspotActor.h"

typedef TGLTFConverter<FGLTFJsonHotspotIndex, const AGLTFHotspotActor*> IGLTFHotspotConverter;

class FGLTFHotspotConverter final : public FGLTFBuilderContext, public IGLTFHotspotConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonHotspotIndex Convert(const AGLTFHotspotActor* HotspotActor) override;
};
