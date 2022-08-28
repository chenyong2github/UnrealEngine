// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

typedef TGLTFConverter<FGLTFJsonLight*, const ULightComponent*> IGLTFLightConverter;

class GLTFEXPORTER_API FGLTFLightConverter final : public FGLTFBuilderContext, public IGLTFLightConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonLight* Convert(const ULightComponent* LightComponent) override;
};
