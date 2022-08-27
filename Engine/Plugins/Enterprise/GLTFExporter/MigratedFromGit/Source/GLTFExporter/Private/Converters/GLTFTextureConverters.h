// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Engine.h"

template <typename... InputTypes>
class FGLTFTextureConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonTextureIndex, InputTypes...>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;
};

class FGLTFTexture2DConverter final : public FGLTFTextureConverter<const UTexture2D*>
{
	using FGLTFTextureConverter::FGLTFTextureConverter;

	virtual FGLTFJsonTextureIndex Convert(const UTexture2D* Texture2D) override;
};

class FGLTFTextureCubeConverter final : public FGLTFTextureConverter<const UTextureCube*, ECubeFace>
{
	using FGLTFTextureConverter::FGLTFTextureConverter;

	virtual FGLTFJsonTextureIndex Convert(const UTextureCube* TextureCube, ECubeFace CubeFace) override;
};

class FGLTFTextureRenderTarget2DConverter final : public FGLTFTextureConverter<const UTextureRenderTarget2D*>
{
	using FGLTFTextureConverter::FGLTFTextureConverter;

	virtual FGLTFJsonTextureIndex Convert(const UTextureRenderTarget2D* RenderTarget2D) override;
};

class FGLTFTextureRenderTargetCubeConverter final : public FGLTFTextureConverter<const UTextureRenderTargetCube*, ECubeFace>
{
	using FGLTFTextureConverter::FGLTFTextureConverter;

	virtual FGLTFJsonTextureIndex Convert(const UTextureRenderTargetCube* RenderTargetCube, ECubeFace CubeFace) override;
};
