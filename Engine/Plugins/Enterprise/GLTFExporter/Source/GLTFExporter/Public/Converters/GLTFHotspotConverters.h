// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Actors/GLTFHotspotActor.h"

typedef TGLTFConverter<FGLTFJsonHotspot*, const AGLTFHotspotActor*> IGLTFHotspotConverter;

class GLTFEXPORTER_API FGLTFHotspotConverter : public FGLTFBuilderContext, public IGLTFHotspotConverter
{
public:

	using FGLTFBuilderContext::FGLTFBuilderContext;

protected:

	virtual FGLTFJsonHotspot* Convert(const AGLTFHotspotActor* HotspotActor) override;
};
