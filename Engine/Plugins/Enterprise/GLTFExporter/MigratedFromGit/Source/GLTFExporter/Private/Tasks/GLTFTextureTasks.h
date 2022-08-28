// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tasks/GLTFTask.h"
#include "Builders/GLTFConvertBuilder.h"

class FGLTFTexture2DTask : public FGLTFTask
{
public:

	FGLTFTexture2DTask(FGLTFConvertBuilder& Builder, const UTexture2D* Texture2D, bool bToSRGB, FGLTFJsonTexture* JsonTexture)
		: FGLTFTask(EGLTFTaskPriority::Texture)
		, Builder(Builder)
		, Texture2D(Texture2D)
		, bToSRGB(bToSRGB)
		, JsonTexture(JsonTexture)
	{
	}

	virtual FString GetName() override;

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	const UTexture2D* Texture2D;
	bool bToSRGB;
	FGLTFJsonTexture* JsonTexture;
};

class FGLTFTextureCubeTask : public FGLTFTask
{
public:

	FGLTFTextureCubeTask(FGLTFConvertBuilder& Builder, const UTextureCube* TextureCube, ECubeFace CubeFace, bool bToSRGB, FGLTFJsonTexture* JsonTexture)
		: FGLTFTask(EGLTFTaskPriority::Texture)
		, Builder(Builder)
		, TextureCube(TextureCube)
		, CubeFace(CubeFace)
		, bToSRGB(bToSRGB)
		, JsonTexture(JsonTexture)
	{
	}

	virtual FString GetName() override;

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	const UTextureCube* TextureCube;
	ECubeFace CubeFace;
	bool bToSRGB;
	FGLTFJsonTexture* JsonTexture;
};

class FGLTFTextureRenderTarget2DTask : public FGLTFTask
{
public:

	FGLTFTextureRenderTarget2DTask(FGLTFConvertBuilder& Builder, const UTextureRenderTarget2D* RenderTarget2D, bool bToSRGB, FGLTFJsonTexture* JsonTexture)
		: FGLTFTask(EGLTFTaskPriority::Texture)
		, Builder(Builder)
		, RenderTarget2D(RenderTarget2D)
		, bToSRGB(bToSRGB)
		, JsonTexture(JsonTexture)
	{
	}

	virtual FString GetName() override;

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	const UTextureRenderTarget2D* RenderTarget2D;
	bool bToSRGB;
	FGLTFJsonTexture* JsonTexture;
};

class FGLTFTextureRenderTargetCubeTask : public FGLTFTask
{
public:

	FGLTFTextureRenderTargetCubeTask(FGLTFConvertBuilder& Builder, const UTextureRenderTargetCube* RenderTargetCube, ECubeFace CubeFace, bool bToSRGB, FGLTFJsonTexture* JsonTexture)
		: FGLTFTask(EGLTFTaskPriority::Texture)
		, Builder(Builder)
		, RenderTargetCube(RenderTargetCube)
		, CubeFace(CubeFace)
		, bToSRGB(bToSRGB)
		, JsonTexture(JsonTexture)
	{
	}

	virtual FString GetName() override;

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	const UTextureRenderTargetCube* RenderTargetCube;
	ECubeFace CubeFace;
	bool bToSRGB;
	FGLTFJsonTexture* JsonTexture;
};

#if WITH_EDITOR

class FGLTFTextureLightMapTask : public FGLTFTask
{
public:

	FGLTFTextureLightMapTask(FGLTFConvertBuilder& Builder, const ULightMapTexture2D* LightMap, FGLTFJsonTexture* JsonTexture)
		: FGLTFTask(EGLTFTaskPriority::Texture)
		, Builder(Builder)
		, LightMap(LightMap)
		, JsonTexture(JsonTexture)
	{
	}

	virtual FString GetName() override;

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	const ULightMapTexture2D* LightMap;
	FGLTFJsonTexture* JsonTexture;
};

#endif
