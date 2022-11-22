// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Engine/TextureDefines.h"

class UTexture;

typedef TGLTFConverter<FGLTFJsonSampler*, const UTexture*> IGLTFTextureSamplerConverter;
typedef TGLTFConverter<FGLTFJsonSampler*, TextureAddress, TextureAddress, TextureFilter, TextureGroup> IGLTFSamplerConverter;

class GLTFEXPORTER_API FGLTFTextureSamplerConverter : public FGLTFBuilderContext, public IGLTFTextureSamplerConverter
{
public:

	using FGLTFBuilderContext::FGLTFBuilderContext;

protected:

	virtual FGLTFJsonSampler* Convert(const UTexture* Texture) override;
};

class GLTFEXPORTER_API FGLTFSamplerConverter : public FGLTFBuilderContext, public IGLTFSamplerConverter
{
public:

	using FGLTFBuilderContext::FGLTFBuilderContext;

protected:

	virtual void Sanitize(TextureAddress& AddressX, TextureAddress& AddressY, TextureFilter& Filter, TextureGroup& LODGroup) override;

	virtual FGLTFJsonSampler* Convert(TextureAddress AddressX, TextureAddress AddressY, TextureFilter Filter, TextureGroup LODGroup) override;
};
