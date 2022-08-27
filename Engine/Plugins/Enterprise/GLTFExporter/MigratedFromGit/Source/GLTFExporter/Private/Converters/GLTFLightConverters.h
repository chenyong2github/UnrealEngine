// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

typedef TGLTFConverter<FGLTFJsonLightIndex, const ULightComponent*> IGLTFLightConverter;

class FGLTFLightConverter final : public FGLTFBuilderContext, public IGLTFLightConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonLightIndex Convert(const ULightComponent* LightComponent) override;
};
