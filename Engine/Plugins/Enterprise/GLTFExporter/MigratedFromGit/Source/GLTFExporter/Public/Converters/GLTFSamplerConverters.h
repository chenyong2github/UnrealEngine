// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

typedef TGLTFConverter<FGLTFJsonSampler*, const UTexture*> IGLTFSamplerConverter;

class FGLTFSamplerConverter final : public FGLTFBuilderContext, public IGLTFSamplerConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonSampler* Convert(const UTexture* Texture) override;
};
