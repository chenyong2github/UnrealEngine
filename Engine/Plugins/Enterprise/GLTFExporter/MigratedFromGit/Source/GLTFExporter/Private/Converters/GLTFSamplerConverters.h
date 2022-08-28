// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

typedef TGLTFConverter<FGLTFJsonSamplerIndex, const UTexture*> IGLTFSamplerConverter;

class FGLTFSamplerConverter final : public FGLTFBuilderContext, public IGLTFSamplerConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonSamplerIndex Convert(const UTexture* Texture) override;
};
