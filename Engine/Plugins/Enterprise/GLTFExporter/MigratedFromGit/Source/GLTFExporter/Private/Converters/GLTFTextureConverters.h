// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

template <typename... InputTypes>
class TGLTFTextureConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonTextureIndex, InputTypes...>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;
};

class FGLTFTexture2DConverter final : public TGLTFTextureConverter<const UTexture2D*, bool>
{
	using TGLTFTextureConverter::TGLTFTextureConverter;

	virtual void Sanitize(const UTexture2D*& Texture2D, bool& bToSRGB) override;

	virtual FGLTFJsonTextureIndex Convert(const UTexture2D* Texture2D, bool bToSRGB) override;
};

class FGLTFTextureCubeConverter final : public TGLTFTextureConverter<const UTextureCube*, ECubeFace, bool>
{
	using TGLTFTextureConverter::TGLTFTextureConverter;

	virtual void Sanitize(const UTextureCube*& TextureCube, ECubeFace& CubeFace, bool& bToSRGB) override;

	virtual FGLTFJsonTextureIndex Convert(const UTextureCube* TextureCube, ECubeFace CubeFace, bool bToSRGB) override;
};

class FGLTFTextureRenderTarget2DConverter final : public TGLTFTextureConverter<const UTextureRenderTarget2D*, bool>
{
	using TGLTFTextureConverter::TGLTFTextureConverter;

	virtual void Sanitize(const UTextureRenderTarget2D*& RenderTarget2D, bool& bToSRGB) override;

	virtual FGLTFJsonTextureIndex Convert(const UTextureRenderTarget2D* RenderTarget2D, bool bToSRGB) override;
};

class FGLTFTextureRenderTargetCubeConverter final : public TGLTFTextureConverter<const UTextureRenderTargetCube*, ECubeFace, bool>
{
	using TGLTFTextureConverter::TGLTFTextureConverter;

	virtual void Sanitize(const UTextureRenderTargetCube*& RenderTargetCube, ECubeFace& CubeFace, bool& bToSRGB) override;

	virtual FGLTFJsonTextureIndex Convert(const UTextureRenderTargetCube* RenderTargetCube, ECubeFace CubeFace, bool bToSRGB) override;
};

class FGLTFTextureLightMapConverter final : public TGLTFTextureConverter<const ULightMapTexture2D*>
{
	using TGLTFTextureConverter::TGLTFTextureConverter;

	virtual FGLTFJsonTextureIndex Convert(const ULightMapTexture2D* LightMap) override;
};
