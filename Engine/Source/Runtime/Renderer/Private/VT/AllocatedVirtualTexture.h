// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VirtualTextureShared.h"
#include "RendererInterface.h"
#include "VirtualTexturing.h"
#include "HAL/ThreadSafeCounter.h"

class FVirtualTextureSpace;
class FVirtualTexturePhysicalSpace;
class FVirtualTextureProducer;
class FVirtualTextureSystem;
class FTexturePageLocks;

class FAllocatedVirtualTexture final : public IAllocatedVirtualTexture
{
public:
	FAllocatedVirtualTexture(FVirtualTextureSystem* InSystem,
		uint32 InFrame,
		const FAllocatedVTDescription& InDesc,
		FVirtualTextureSpace* InSpace,
		FVirtualTextureProducer* const* InProducers,
		uint32 InBlockWidthInTiles,
		uint32 InBlockHeightInTiles,
		uint32 InWidthInBlocks,
		uint32 InHeightInBlocks,
		uint32 InDepthInTiles);

	virtual ~FAllocatedVirtualTexture();

	void Release(FVirtualTextureSystem* System);

	// begin IAllocatedVirtualTexture
	virtual FRHITexture* GetPageTableTexture(uint32 InPageTableIndex) const override;
	virtual FRHITexture* GetPhysicalTexture(uint32 InLayerIndex) const override;
	virtual FRHIShaderResourceView* GetPhysicalTextureView(uint32 InLayerIndex, bool bSRGB) const override;
	virtual uint32 GetPhysicalTextureSize(uint32 InLayerIndex) const override;
	virtual void GetPackedPageTableUniform(FUintVector4* OutUniform, bool bApplyBlockScale) const override;
	virtual void GetPackedUniform(FUintVector4* OutUniform, uint32 LayerIndex) const override;
	virtual void Destroy(FVirtualTextureSystem* System) override;
	// end IAllocatedVirtualTexture

	inline void IncrementRefCount() { RefCount.Increment(); }

	inline uint32 GetFrameAllocated() const { return FrameAllocated; }
	inline FVirtualTextureSpace* GetSpace() const { return Space; }
	inline FVirtualTexturePhysicalSpace* GetPhysicalSpace(uint32 InLayer) const { checkSlow(InLayer < Description.NumLayers); return PhysicalSpace[InLayer]; }
	inline uint32 GetNumUniqueProducers() const { return NumUniqueProducers; }
	inline uint32 GetUniqueProducerIndexForLayer(uint32 InLayer) const { return UniqueProducerIndexForLayer[InLayer]; }
	inline const FVirtualTextureProducerHandle& GetUniqueProducerHandle(uint32 InProducerIndex) const { checkSlow(InProducerIndex < NumUniqueProducers); return UniqueProducerHandles[InProducerIndex]; }
	inline uint8 GetUniqueProducerMipBias(uint32 InProducerIndex) const { checkSlow(InProducerIndex < NumUniqueProducers); return UniqueProducerMipBias[InProducerIndex]; }

private:
	uint32 AddUniqueProducer(const FVirtualTextureProducerHandle& InHandle, FVirtualTextureProducer* InProducer);

	FVirtualTextureSpace* Space;
	FVirtualTexturePhysicalSpace* PhysicalSpace[VIRTUALTEXTURE_SPACE_MAXLAYERS];
	FVirtualTextureProducerHandle UniqueProducerHandles[VIRTUALTEXTURE_SPACE_MAXLAYERS];
	uint8 UniqueProducerIndexForLayer[VIRTUALTEXTURE_SPACE_MAXLAYERS];
	uint8 UniqueProducerMipBias[VIRTUALTEXTURE_SPACE_MAXLAYERS];
	FThreadSafeCounter RefCount;
	uint32 FrameAllocated;
	uint8 NumUniqueProducers;
};
