// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VirtualTextureShared.h"
#include "TexturePagePool.h"
#include "RendererInterface.h"
#include "VirtualTexturing.h"

struct FVTPhysicalSpaceDescription
{
	uint32 TileSize;
	uint8 Dimensions;
	uint8 NumLayers;
	TEnumAsByte<EPixelFormat> Format[VIRTUALTEXTURE_SPACE_MAXLAYERS];
	bool bContinuousUpdate;
};

inline bool operator==(const FVTPhysicalSpaceDescription& Lhs, const FVTPhysicalSpaceDescription& Rhs)
{
	if (Lhs.TileSize != Rhs.TileSize || 
		Lhs.NumLayers != Rhs.NumLayers || 
		Lhs.Dimensions != Rhs.Dimensions || 
		Lhs.bContinuousUpdate != Rhs.bContinuousUpdate)
	{
		return false;
	}
	for (int32 Layer = 0; Layer < Lhs.NumLayers; ++Layer)
	{
		if (Lhs.Format[Layer] != Rhs.Format[Layer])
		{
			return false;
		}
	}
	return true;
}
inline bool operator!=(const FVTPhysicalSpaceDescription& Lhs, const FVTPhysicalSpaceDescription& Rhs)
{
	return !operator==(Lhs, Rhs);
}

class FVirtualTexturePhysicalSpace final : public FRenderResource
{
public:
	FVirtualTexturePhysicalSpace(const FVTPhysicalSpaceDescription& InDesc, uint16 InID);
	virtual ~FVirtualTexturePhysicalSpace();

	inline const FVTPhysicalSpaceDescription& GetDescription() const { return Description; }
	inline EPixelFormat GetFormat(int32 Layer) const { return Description.Format[Layer]; }
	inline uint16 GetID() const { return ID; }
	inline uint32 GetNumTiles() const { return TextureSizeInTiles * TextureSizeInTiles; }
	inline uint32 GetSizeInTiles() const { return TextureSizeInTiles; }
	inline uint32 GetTextureSize() const { return TextureSizeInTiles * Description.TileSize; }
	inline FIntVector GetPhysicalLocation(uint16 pAddress) const { return FIntVector(pAddress % TextureSizeInTiles, pAddress / TextureSizeInTiles, 0); }

	// 16bit page tables allocate 6bits to address TileX/Y, so can only address tiles from 0-63
	inline bool DoesSupport16BitPageTable() const { return TextureSizeInTiles <= 64u; }

	uint32 GetSizeInBytes() const;

	inline const FTexturePagePool& GetPagePool() const { return Pool; }
	inline FTexturePagePool& GetPagePool() { return Pool; }

	// FRenderResource interface
	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;

	inline uint32 AddRef() { return ++NumRefs; }
	inline uint32 Release() { check(NumRefs > 0u); return --NumRefs; }
	inline uint32 GetRefCount() const { return NumRefs; }

	FRHITexture* GetPhysicalTexture(int32 Layer) const
	{
		check(PooledRenderTarget[Layer].IsValid());
		return PooledRenderTarget[Layer]->GetRenderTargetItem().ShaderResourceTexture;
	}

	FRHIShaderResourceView* GetPhysicalTextureSRV(int32 Layer, bool bSRGB) const
	{
		return bSRGB ? TextureSRV_SRGB[Layer] : TextureSRV[Layer];
	}

	FRHIUnorderedAccessView* GetPhysicalTextureUAV(int32 Layer) const
	{
		return TextureUAV[Layer];
	}
	
	TRefCountPtr<IPooledRenderTarget> GetPhysicalTexturePooledRenderTarget(int32 Layer) const
	{
		check(PooledRenderTarget[Layer].IsValid());
		return PooledRenderTarget[Layer];
	}

#if STATS
	inline void ResetWorkingSetSize() { WorkingSetSize.Reset(); }
	inline void IncrementWorkingSetSize(int32 Amount) { WorkingSetSize.Add(Amount); }
	void UpdateWorkingSetStat();
#else // STATS
	inline void ResetWorkingSetSize() {}
	inline void IncrementWorkingSetSize(int32 Amount) {}
	inline void UpdateWorkingSetStat() {}
#endif // !STATS

private:
	FVTPhysicalSpaceDescription Description;
	FTexturePagePool Pool;
	TRefCountPtr<IPooledRenderTarget> PooledRenderTarget[VIRTUALTEXTURE_SPACE_MAXLAYERS];
	FShaderResourceViewRHIRef TextureSRV[VIRTUALTEXTURE_SPACE_MAXLAYERS];
	FShaderResourceViewRHIRef TextureSRV_SRGB[VIRTUALTEXTURE_SPACE_MAXLAYERS];
	FUnorderedAccessViewRHIRef TextureUAV[VIRTUALTEXTURE_SPACE_MAXLAYERS];

	uint32 TextureSizeInTiles;
	uint32 NumRefs;
	uint16 ID;
	bool bPageTableLimit; // True if the physical size was limited by the page table format requested
	bool bGpuTextureLimit; // True if the physical size was limited by the maximum GPU texture size

#if STATS
	TStatId WorkingSetSizeStatID;
	FThreadSafeCounter WorkingSetSize;
#endif // STATS
};
