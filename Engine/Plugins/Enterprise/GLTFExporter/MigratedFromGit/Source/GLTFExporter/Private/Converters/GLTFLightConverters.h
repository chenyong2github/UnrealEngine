// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Engine.h"

class FGLTFLightComponentConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonLightIndex, const ULightComponent*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonLightIndex Convert(const ULightComponent* LightComponent) override final;
};
