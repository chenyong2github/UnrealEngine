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

	uint32 GetResourceBulkDataSize() const override
	{
		return MipSize[FirstMipIdx];
	}

	void Discard() override;
	void MergeMips(int32 NumMips);

	void** GetMipData() { return MipData; }
	uint32* GetMipSize() { return MipSize; }
	int32 GetFirstMipIdx() const { return FirstMipIdx; }

protected:

	void* MipData[MAX_TEXTURE_MIP_COUNT];
	uint32 MipSize[MAX_TEXTURE_MIP_COUNT];
	int32 FirstMipIdx;
};


class FTexture3DResource : public FStreamableTextureResource
{
public: 

	FTexture3DResource(UVolumeTexture* InOwner, const FStreamableRenderResourceState& InState);

private:

	void CreateTexture() final override;
	void CreatePartiallyResidentTexture() final override;

#if STATS
	virtual void CalcRequestedMipsSize() final override;
#endif

protected:
	FVolumeTextureBulkData InitialData;

};
