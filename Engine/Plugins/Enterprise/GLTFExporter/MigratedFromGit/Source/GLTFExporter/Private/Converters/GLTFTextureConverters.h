// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Engine.h"

class FGLTFTextureSamplerConverter final : public TGLTFConverter<FGLTFJsonSamplerIndex, const UTexture*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonSamplerIndex Convert(const FString& Name, const UTexture* Texture) override;
};

class FGLTFTexture2DConverter final : public TGLTFConverter<FGLTFJsonTextureIndex, const UTexture2D*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonTextureIndex Convert(const FString& Name, const UTexture2D* Texture2D) override;
};

class FGLTFTextureCubeConverter final : public TGLTFConverter<FGLTFJsonTextureIndex, const UTextureCube*, ECubeFace>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonTextureIndex Convert(const FString& Name, const UTextureCube* TextureCube, ECubeFace CubeFace) override;
};

class FGLTFTextureRenderTarget2DConverter final : public TGLTFConverter<FGLTFJsonTextureIndex, const UTextureRenderTarget2D*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonTextureIndex Convert(const FString& Name, const UTextureRenderTarget2D* RenderTarget2D) override;
};

class FGLTFTextureRenderTargetCubeConverter final : public TGLTFConverter<FGLTFJsonTextureIndex, const UTextureRenderTargetCube*, ECubeFace>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonTextureIndex Convert(const FString& Name, const UTextureRenderTargetCube* RenderTargetCube, ECubeFace CubeFace) override;
};
