// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Engine.h"

class FGLTFTexture2DConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonTextureIndex, const UTexture2D*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonTextureIndex Convert(const UTexture2D* Texture2D) override final;
};

class FGLTFTextureCubeConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonTextureIndex, const UTextureCube*, ECubeFace>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonTextureIndex Convert(const UTextureCube* TextureCube, ECubeFace CubeFace) override final;
};

class FGLTFTextureRenderTarget2DConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonTextureIndex, const UTextureRenderTarget2D*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonTextureIndex Convert(const UTextureRenderTarget2D* RenderTarget2D) override final;
};

class FGLTFTextureRenderTargetCubeConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonTextureIndex, const UTextureRenderTargetCube*, ECubeFace>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonTextureIndex Convert(const UTextureRenderTargetCube* RenderTargetCube, ECubeFace CubeFace) override final;
};
