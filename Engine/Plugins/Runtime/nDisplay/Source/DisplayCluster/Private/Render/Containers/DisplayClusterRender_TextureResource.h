// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "TextureResource.h"
#include "Templates/SharedPointer.h"

/**
 * Creating a texture resource from memory data
 * Also, this texture data will be stored in memory if bHasCPUAccess is true.
 */
class FDisplayClusterRender_TextureResource
	: public FTextureResource
	, public TSharedFromThis<FDisplayClusterRender_TextureResource, ESPMode::ThreadSafe>
{
public:
	FDisplayClusterRender_TextureResource(const void* InTextureData, const uint32 InComponentDepth, const uint32 InBitDepth, uint32_t InWidth, uint32_t InHeight, bool bInHasCPUAccess);
	virtual ~FDisplayClusterRender_TextureResource();

	//~FTextureResource
	virtual void InitRHI() override;
	//~~ FTextureResource

	/** Release texture data from memory. */
	void ReleaseTextureData();

	/** Initializes the render resource. */
	void InitializeRenderResource();

	/** Prepares the resource for deletion. */
	void ReleaseRenderResource();

	/** Get ptr to resource data in memory. The variable bHasCPUAccess must be true. */
	void* GetTextureData() const
	{
		return TextureData;
	}

	/** Get texture width. */
	uint32_t GetWidth() const
	{
		return Width;
	}

	/** Get texture height. */
	uint32_t GetHeight() const
	{
		return Height;
	}

	/** Get texture pixel format. */
	EPixelFormat GetPixelFormat() const
	{
		return PixelFormat;
	}

private:
	// RAW data of the texture. Mips not supported now
	void* TextureData;
	
	// Width of the texture.
	uint32_t Width;
	
	// Height of the texture.
	uint32_t Height;
	
	// Format in which mip data is stored.
	EPixelFormat PixelFormat;
	
	// has CPU access to texture data, dont release from memory
	bool bHasCPUAccess;
};
