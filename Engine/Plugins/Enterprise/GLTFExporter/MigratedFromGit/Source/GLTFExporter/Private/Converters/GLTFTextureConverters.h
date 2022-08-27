// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

// TODO: generalize parameter bToSRGB to SamplerType (including normalmap unpacking)

// TODO: remove lightmap-specific converter (since it won't work in runtime)

typedef TGLTFConverter<FGLTFJsonTextureIndex, const UTexture2D*, bool> IGLTFTexture2DConverter;
typedef TGLTFConverter<FGLTFJsonTextureIndex, const UTextureCube*, ECubeFace, bool> IGLTFTextureCubeConverter;
typedef TGLTFConverter<FGLTFJsonTextureIndex, const UTextureRenderTarget2D*, bool> IGLTFTextureRenderTarget2DConverter;
typedef TGLTFConverter<FGLTFJsonTextureIndex, const UTextureRenderTargetCube*, ECubeFace, bool> IGLTFTextureRenderTargetCubeConverter;
typedef TGLTFConverter<FGLTFJsonTextureIndex, const ULightMapTexture2D*> IGLTFTextureLightMapConverter;

class FGLTFTexture2DConverter final : public FGLTFBuilderContext, public IGLTFTexture2DConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual void Sanitize(const UTexture2D*& Texture2D, bool& bToSRGB) override;

	virtual FGLTFJsonTextureIndex Convert(const UTexture2D* Texture2D, bool bToSRGB) override;
};

class FGLTFTextureCubeConverter final : public FGLTFBuilderContext, public IGLTFTextureCubeConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual void Sanitize(const UTextureCube*& TextureCube, ECubeFace& CubeFace, bool& bToSRGB) override;

	virtual FGLTFJsonTextureIndex Convert(const UTextureCube* TextureCube, ECubeFace CubeFace, bool bToSRGB) override;
};

class FGLTFTextureRenderTarget2DConverter final : public FGLTFBuilderContext, public IGLTFTextureRenderTarget2DConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual void Sanitize(const UTextureRenderTarget2D*& RenderTarget2D, bool& bToSRGB) override;

	virtual FGLTFJsonTextureIndex Convert(const UTextureRenderTarget2D* RenderTarget2D, bool bToSRGB) override;
};

class FGLTFTextureRenderTargetCubeConverter final : public FGLTFBuilderContext, public IGLTFTextureRenderTargetCubeConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual void Sanitize(const UTextureRenderTargetCube*& RenderTargetCube, ECubeFace& CubeFace, bool& bToSRGB) override;

	virtual FGLTFJsonTextureIndex Convert(const UTextureRenderTargetCube* RenderTargetCube, ECubeFace CubeFace, bool bToSRGB) override;
};

class FGLTFTextureLightMapConverter final : public FGLTFBuilderContext, public IGLTFTextureLightMapConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonTextureIndex Convert(const ULightMapTexture2D* LightMap) override;
};
