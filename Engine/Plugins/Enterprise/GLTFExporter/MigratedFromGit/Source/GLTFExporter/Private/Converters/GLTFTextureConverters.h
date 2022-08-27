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

class FGLTFTexture2DConverter : public FGLTFTextureConverter<const UTexture2D*>
{
	using FGLTFTextureConverter::FGLTFTextureConverter;

	virtual FGLTFJsonTextureIndex Convert(const UTexture2D* Texture2D) override final;
};

class FGLTFTextureCubeConverter : public FGLTFTextureConverter<const UTextureCube*, ECubeFace>
{
	using FGLTFTextureConverter::FGLTFTextureConverter;

	virtual FGLTFJsonTextureIndex Convert(const UTextureCube* TextureCube, ECubeFace CubeFace) override final;
};

class FGLTFTextureRenderTarget2DConverter : public FGLTFTextureConverter<const UTextureRenderTarget2D*>
{
	using FGLTFTextureConverter::FGLTFTextureConverter;

	virtual FGLTFJsonTextureIndex Convert(const UTextureRenderTarget2D* RenderTarget2D) override final;
};

class FGLTFTextureRenderTargetCubeConverter : public FGLTFTextureConverter<const UTextureRenderTargetCube*, ECubeFace>
{
	using FGLTFTextureConverter::FGLTFTextureConverter;

	virtual FGLTFJsonTextureIndex Convert(const UTextureRenderTargetCube* RenderTargetCube, ECubeFace CubeFace) override final;
};
