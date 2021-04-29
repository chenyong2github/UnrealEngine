// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/IDisplayClusterRenderTexture.h"

// Runtime texture resoure
class FDisplayClusterRenderTexture
	: public IDisplayClusterRenderTexture
	, public FTexture
{
public:
	FDisplayClusterRenderTexture();
	virtual ~FDisplayClusterRenderTexture();

	virtual void InitRHI() override;

public:
	virtual void CreateTexture(EPixelFormat InPixelFormat, uint32_t InWidth, uint32_t InHeight, void* InTextureData, bool bInHasCPUAccess = false) override;

	virtual void* GetData() const override
	{ return Data; }

	virtual uint32_t GetWidth() const override
	{ return Width; }

	virtual uint32_t GetHeight() const override
	{ return Height; }

	virtual uint32_t GetTotalPoints() const override
	{ return Width * Height; }

	virtual EPixelFormat GetPixelFormat() const override
	{ return PixelFormat; }

	virtual bool IsValid() const override
	{ return (Width > 0) && (Height > 0); }

	virtual FRHITexture* GetRHITexture() const override
	{
		return TextureRHI.GetReference();
	}

	void ReleaseTextureData();

private:
	void *Data = nullptr;
	uint32_t Width = 0;
	uint32_t Height = 0;
	EPixelFormat PixelFormat= EPixelFormat(0);

	// has CPU access to texture data, dont release from memory
	bool bHasCPUAccess = false;
};
