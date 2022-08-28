// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Engine.h"

class FGLTFTextureSamplerConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonSamplerIndex, const UTexture*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonSamplerIndex Convert(const UTexture* Texture) override final;
};
