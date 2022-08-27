// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tasks/GLTFTask.h"
#include "Builders/GLTFConvertBuilder.h"

class FGLTFTexture2DTask : public FGLTFTask
{
public:

	FGLTFTexture2DTask(FGLTFConvertBuilder& Builder, const UTexture2D* Texture2D, bool bToSRGB, FGLTFJsonTextureIndex TextureIndex)
		: FGLTFTask(EGLTFTaskPriority::Texture)
		, Builder(Builder)
		, Texture2D(Texture2D)
		, bToSRGB(bToSRGB)
		, TextureIndex(TextureIndex)
	{
	}

	virtual FString GetName() override;

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	const UTexture2D* Texture2D;
	bool bToSRGB;
	const FGLTFJsonTextureIndex TextureIndex;
};

class FGLTFTextureCubeTask : public FGLTFTask
{
public:

	FGLTFTextureCubeTask(FGLTFConvertBuilder& Builder, const UTextureCube* TextureCube, ECubeFace CubeFace, bool bToSRGB, FGLTFJsonTextureIndex TextureIndex)
		: FGLTFTask(EGLTFTaskPriority::Texture)
		, Builder(Builder)
		, TextureCube(TextureCube)
		, CubeFace(CubeFace)
		, bToSRGB(bToSRGB)
		, TextureIndex(TextureIndex)
	{
	}

	virtual FString GetName() override;

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	const UTextureCube* TextureCube;
	ECubeFace CubeFace;
	bool bToSRGB;
	const FGLTFJsonTextureIndex TextureIndex;
};

class FGLTFTextureRenderTarget2DTask : public FGLTFTask
{
public:

	FGLTFTextureRenderTarget2DTask(FGLTFConvertBuilder& Builder, const UTextureRenderTarget2D* RenderTarget2D, bool bToSRGB, FGLTFJsonTextureIndex TextureIndex)
		: FGLTFTask(EGLTFTaskPriority::Texture)
		, Builder(Builder)
		, RenderTarget2D(RenderTarget2D)
		, bToSRGB(bToSRGB)
		, TextureIndex(TextureIndex)
	{
	}

	virtual FString GetName() override;

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	const UTextureRenderTarget2D* RenderTarget2D;
	bool bToSRGB;
	const FGLTFJsonTextureIndex TextureIndex;
};

class FGLTFTextureRenderTargetCubeTask : public FGLTFTask
{
public:

	FGLTFTextureRenderTargetCubeTask(FGLTFConvertBuilder& Builder, const UTextureRenderTargetCube* RenderTargetCube, ECubeFace CubeFace, bool bToSRGB, FGLTFJsonTextureIndex TextureIndex)
		: FGLTFTask(EGLTFTaskPriority::Texture)
		, Builder(Builder)
		, RenderTargetCube(RenderTargetCube)
		, CubeFace(CubeFace)
		, bToSRGB(bToSRGB)
		, TextureIndex(TextureIndex)
	{
	}

	virtual FString GetName() override;

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	const UTextureRenderTargetCube* RenderTargetCube;
	ECubeFace CubeFace;
	bool bToSRGB;
	const FGLTFJsonTextureIndex TextureIndex;
};

#if WITH_EDITOR

class FGLTFTextureLightMapTask : public FGLTFTask
{
public:

	FGLTFTextureLightMapTask(FGLTFConvertBuilder& Builder, const ULightMapTexture2D* LightMap, FGLTFJsonTextureIndex TextureIndex)
		: FGLTFTask(EGLTFTaskPriority::Texture)
		, Builder(Builder)
		, LightMap(LightMap)
		, TextureIndex(TextureIndex)
	{
	}

	virtual FString GetName() override;

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	const ULightMapTexture2D* LightMap;
	const FGLTFJsonTextureIndex TextureIndex;
};

#endif
