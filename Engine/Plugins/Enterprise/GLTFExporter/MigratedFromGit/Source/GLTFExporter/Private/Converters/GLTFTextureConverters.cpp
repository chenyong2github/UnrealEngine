// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFTextureConverters.h"
#include "Tasks/GLTFTextureTasks.h"

FGLTFJsonTextureIndex FGLTFTexture2DConverter::Convert(const UTexture2D* Texture2D, bool bToSRGB)
{
	if (Builder.ExportOptions->TextureImageFormat != EGLTFTextureImageFormat::None)
	{
		const FGLTFJsonTextureIndex TextureIndex = Builder.AddTexture();
		Builder.SetupTask<FGLTFTexture2DTask>(Builder, Texture2D, bToSRGB, TextureIndex);
		return TextureIndex;
	}

	return FGLTFJsonTextureIndex(INDEX_NONE);
}

FGLTFJsonTextureIndex FGLTFTextureCubeConverter::Convert(const UTextureCube* TextureCube, ECubeFace CubeFace, bool bToSRGB)
{
	if (Builder.ExportOptions->TextureImageFormat != EGLTFTextureImageFormat::None)
	{
		const FGLTFJsonTextureIndex TextureIndex = Builder.AddTexture();
		Builder.SetupTask<FGLTFTextureCubeTask>(Builder, TextureCube, CubeFace, bToSRGB, TextureIndex);
		return TextureIndex;
	}

	return FGLTFJsonTextureIndex(INDEX_NONE);
}

FGLTFJsonTextureIndex FGLTFTextureRenderTarget2DConverter::Convert(const UTextureRenderTarget2D* RenderTarget2D, bool bToSRGB)
{
	if (Builder.ExportOptions->TextureImageFormat != EGLTFTextureImageFormat::None)
	{
		const FGLTFJsonTextureIndex TextureIndex = Builder.AddTexture();
		Builder.SetupTask<FGLTFTextureRenderTarget2DTask>(Builder, RenderTarget2D, bToSRGB, TextureIndex);
		return TextureIndex;
	}

	return FGLTFJsonTextureIndex(INDEX_NONE);
}

FGLTFJsonTextureIndex FGLTFTextureRenderTargetCubeConverter::Convert(const UTextureRenderTargetCube* RenderTargetCube, ECubeFace CubeFace, bool bToSRGB)
{
	if (Builder.ExportOptions->TextureImageFormat != EGLTFTextureImageFormat::None)
	{
		const FGLTFJsonTextureIndex TextureIndex = Builder.AddTexture();
		Builder.SetupTask<FGLTFTextureRenderTargetCubeTask>(Builder, RenderTargetCube, CubeFace, bToSRGB, TextureIndex);
		return TextureIndex;
	}

	return FGLTFJsonTextureIndex(INDEX_NONE);
}

FGLTFJsonTextureIndex FGLTFTextureLightMapConverter::Convert(const ULightMapTexture2D* LightMap)
{
#if WITH_EDITOR
	if (Builder.ExportOptions->TextureImageFormat != EGLTFTextureImageFormat::None)
	{
		const FGLTFJsonTextureIndex TextureIndex = Builder.AddTexture();
		Builder.SetupTask<FGLTFTextureLightMapTask>(Builder, LightMap, TextureIndex);
		return TextureIndex;
	}
#endif

	return FGLTFJsonTextureIndex(INDEX_NONE);
}
