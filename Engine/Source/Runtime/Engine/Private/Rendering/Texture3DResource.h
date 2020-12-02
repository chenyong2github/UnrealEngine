// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/*=============================================================================
	Texture3DResource.h: Implementation of FTexture3DResource used by streamable UVolumeTexture.
=============================================================================*/

#include "CoreMinimal.h"
#include "Rendering/StreamableTextureResource.h"
#include "Containers/ResourceArray.h"

class UVolumeTexture;

class FVolumeTextureBulkData : public FResourceBulkDataInterface
{
public:

	static const uint32 MALLOC_ALIGNMENT = 16;

	FVolumeTextureBulkData(int32 InFirstMipIdx)
	: FirstMipIdx(InFirstMipIdx)
	{
		FMemory::Memzero(MipData, sizeof(MipData));
		FMemory::Memzero(MipSize, sizeof(MipSize));
	}

	~FVolumeTextureBulkData()
	{ 
		Discard();
	}

	const void* GetResourceBulkData() const override
	{
		return MipData[FirstMipIdx];
	}

	void* GetResourceBulkData()
	{
		return MipData[FirstMipIdx];
	}

	uint32 GetResourceBulkDataSize() const override
	{

		return (uint32)MipSize[FirstMipIdx];
	}

	void Discard() override;
	void MergeMips(int32 NumMips);

	void** GetMipData() { return MipData; }
	uint64* GetMipSize() { return MipSize; }
	int32 GetFirstMipIdx() const { return FirstMipIdx; }

protected:

	void* MipData[MAX_TEXTURE_MIP_COUNT];
	uint64 MipSize[MAX_TEXTURE_MIP_COUNT];
	int32 FirstMipIdx;
};


class FTexture3DResource : public FStreamableTextureResource
{
public: 

	FTexture3DResource(UVolumeTexture* InOwner, const FStreamableRenderResourceState& InState);

	// Dynamic cast methods.
	ENGINE_API virtual FTexture3DResource* GetTexture3DResource() override { return this; }
	// Dynamic cast methods (const).
	ENGINE_API virtual const FTexture3DResource* GetTexture3DResource() const override { return this; }

	/** Returns the platform mip size for the given mip count. */
	virtual uint64 GetPlatformMipsSize(uint32 NumMips) const override;

private:

	void CreateTexture() final override;
	void CreatePartiallyResidentTexture() final override;

protected:
	FVolumeTextureBulkData InitialData;

};
