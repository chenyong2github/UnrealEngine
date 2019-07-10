// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AllocatedVirtualTexture.h"
#include "VirtualTextureSystem.h"
#include "VirtualTextureSpace.h"
#include "VirtualTexturePhysicalSpace.h"

FAllocatedVirtualTexture::FAllocatedVirtualTexture(FVirtualTextureSystem* InSystem,
	uint32 InFrame,
	const FAllocatedVTDescription& InDesc,
	FVirtualTextureSpace* InSpace,
	FVirtualTextureProducer* const* InProducers,
	uint32 InBlockWidthInTiles,
	uint32 InBlockHeightInTiles,
	uint32 InWidthInBlocks,
	uint32 InHeightInBlocks,
	uint32 InDepthInTiles)
	: IAllocatedVirtualTexture(InDesc, InSpace->GetID(), InSpace->GetDescription().Format, InBlockWidthInTiles, InBlockHeightInTiles, InWidthInBlocks, InHeightInBlocks, InDepthInTiles)
	, Space(InSpace)
	, RefCount(1)
	, FrameAllocated(InFrame)
	, NumUniqueProducers(0u)
{
	check(IsInRenderingThread());
	FMemory::Memzero(PhysicalSpace);
	FMemory::Memzero(UniqueProducerHandles);
	FMemory::Memzero(UniqueProducerMipBias);

	for (uint32 LayerIndex = 0u; LayerIndex < Description.NumLayers; ++LayerIndex)
	{
		FVirtualTextureProducer* Producer = InProducers[LayerIndex];
		if (Producer)
		{
			PhysicalSpace[LayerIndex] = Producer->GetPhysicalSpace(InDesc.LocalLayerToProduce[LayerIndex]);
			UniqueProducerIndexForLayer[LayerIndex] = AddUniqueProducer(InDesc.ProducerHandle[LayerIndex], Producer);
		}
		else
		{
			UniqueProducerIndexForLayer[LayerIndex] = 0xff;
		}
	}

	// Must have at least 1 valid layer/producer
	check(NumUniqueProducers > 0u);

	// Max level of overall allocated VT is limited by size in tiles
	// With multiple layers of different sizes, some layers may have mips smaller than a single tile
	MaxLevel = FMath::Min(MaxLevel, FMath::CeilLogTwo(FMath::Max(GetWidthInTiles(), GetHeightInTiles())));

	// Lock lowest resolution mip from each producer
	// Depending on the block dimensions of the producers that make up this allocated VT, different allocated VTs may need to lock different low resolution mips from the same producer
	// In the common case where block dimensions match, same mip will be locked by all allocated VTs that make use of the same producer
	for (uint32 LayerIndex = 0u; LayerIndex < Description.NumLayers; ++LayerIndex)
	{
		FVirtualTextureProducer* Producer = InProducers[LayerIndex];
		if (Producer && Producer->GetDescription().bPersistentHighestMip)
		{
			const uint32 ProducerIndex = UniqueProducerIndexForLayer[LayerIndex];
			const uint32 MipBias = UniqueProducerMipBias[ProducerIndex];
			check(MipBias <= MaxLevel);
			const uint32 Local_vLevel = MaxLevel - MipBias;
			check(Local_vLevel <= Producer->GetMaxLevel());

			const uint32 RootWidthInTiles = FMath::Max(Producer->GetWidthInTiles() >> Local_vLevel, 1u);
			const uint32 RootHeightInTiles = FMath::Max(Producer->GetHeightInTiles() >> Local_vLevel, 1u);
			for (uint32 TileY = 0u; TileY < RootHeightInTiles; ++TileY)
			{
				for (uint32 TileX = 0u; TileX < RootWidthInTiles; ++TileX)
				{
					const uint32 Local_vAddress = FMath::MortonCode2(TileX) | (FMath::MortonCode2(TileY) << 1);
					const FVirtualTextureLocalTile TileToUnlock(InDesc.ProducerHandle[LayerIndex], Local_vAddress, Local_vLevel);
					InSystem->LockTile(TileToUnlock);
				}
			}
		}
	}

	VirtualAddress = Space->AllocateVirtualTexture(this);
}

FAllocatedVirtualTexture::~FAllocatedVirtualTexture()
{
}

void FAllocatedVirtualTexture::Destroy(FVirtualTextureSystem* System)
{
	const int32 NewRefCount = RefCount.Decrement();
	check(NewRefCount >= 0);
	if (NewRefCount == 0)
	{
		System->ReleaseVirtualTexture(this);
	}
}

void FAllocatedVirtualTexture::Release(FVirtualTextureSystem* System)
{
	check(IsInRenderingThread());
	check(RefCount.GetValue() == 0);

	// Unlock any locked tiles
	for (uint32 LayerIndex = 0u; LayerIndex < Description.NumLayers; ++LayerIndex)
	{
		const FVirtualTextureProducerHandle ProducerHandle = GetProducerHandle(LayerIndex);
		FVirtualTextureProducer* Producer = System->FindProducer(ProducerHandle);
		if (Producer && Producer->GetDescription().bPersistentHighestMip)
		{
			const uint32 ProducerIndex = UniqueProducerIndexForLayer[LayerIndex];
			const uint32 MipBias = UniqueProducerMipBias[ProducerIndex];
			check(MipBias <= MaxLevel);
			const uint32 Local_vLevel = MaxLevel - MipBias;
			check(Local_vLevel <= Producer->GetMaxLevel());

			const uint32 RootWidthInTiles = FMath::Max(Producer->GetWidthInTiles() >> Local_vLevel, 1u);
			const uint32 RootHeightInTiles = FMath::Max(Producer->GetHeightInTiles() >> Local_vLevel, 1u);
			for (uint32 TileY = 0u; TileY < RootHeightInTiles; ++TileY)
			{
				for (uint32 TileX = 0u; TileX < RootWidthInTiles; ++TileX)
				{
					const uint32 Local_vAddress = FMath::MortonCode2(TileX) | (FMath::MortonCode2(TileY) << 1);
					const FVirtualTextureLocalTile TileToUnlock(ProducerHandle, Local_vAddress, Local_vLevel);
					System->UnlockTile(TileToUnlock, Producer);
				}
			}
		}

		// Physical pool needs to evict all pages that belong to this VT's space
		// TODO - could improve this to only evict pages belonging to this VT
		if (PhysicalSpace[LayerIndex])
		{
			FTexturePageMap& PageMap = Space->GetPageMap(LayerIndex);
			PhysicalSpace[LayerIndex]->GetPagePool().UnmapAllPagesForSpace(System, Space->GetID());
			PageMap.VerifyPhysicalSpaceUnmapped(PhysicalSpace[LayerIndex]->GetID());
		}
	}

	Space->FreeVirtualTexture(this);
	System->RemoveAllocatedVT(this);
	System->ReleaseSpace(Space);

	delete this;
}

uint32 FAllocatedVirtualTexture::AddUniqueProducer(const FVirtualTextureProducerHandle& InHandle, FVirtualTextureProducer* InProducer)
{
	for (uint32 Index = 0u; Index < NumUniqueProducers; ++Index)
	{
		if (UniqueProducerHandles[Index] == InHandle)
		{
			return Index;
		}
	}
	const uint32 Index = NumUniqueProducers++;
	check(Index < VIRTUALTEXTURE_SPACE_MAXLAYERS);
	
	const FVTProducerDescription& ProducerDesc = InProducer->GetDescription();

	// maybe these values should just be set by producers, rather than also set on AllocatedVT desc
	check(ProducerDesc.Dimensions == Description.Dimensions);
	check(ProducerDesc.TileSize == Description.TileSize);
	check(ProducerDesc.TileBorderSize == Description.TileBorderSize);

	const uint32 BlockSizeInTiles = FMath::Max(BlockWidthInTiles, BlockHeightInTiles);
	const uint32 ProducerBlockSizeInTiles = FMath::Max(ProducerDesc.BlockWidthInTiles, ProducerDesc.BlockHeightInTiles);
	const uint32 MipBias = FMath::CeilLogTwo(BlockSizeInTiles / ProducerBlockSizeInTiles);

	check((BlockSizeInTiles / ProducerBlockSizeInTiles) * ProducerBlockSizeInTiles == BlockSizeInTiles);
	check(ProducerDesc.BlockWidthInTiles << MipBias == BlockWidthInTiles);
	check(ProducerDesc.BlockHeightInTiles << MipBias == BlockHeightInTiles);

	MaxLevel = FMath::Max<uint32>(MaxLevel, ProducerDesc.MaxLevel + MipBias);

	UniqueProducerHandles[Index] = InHandle;
	UniqueProducerMipBias[Index] = MipBias;
	
	return Index;
}

FRHITexture* FAllocatedVirtualTexture::GetPageTableTexture(uint32 InPageTableIndex) const
{
	return Space->GetPageTableTexture(InPageTableIndex);
}

FRHITexture* FAllocatedVirtualTexture::GetPhysicalTexture(uint32 InLayerIndex) const
{
	if (InLayerIndex < Description.NumLayers)
	{
		const FVirtualTexturePhysicalSpace* LayerSpace = PhysicalSpace[InLayerIndex];
		return LayerSpace ? LayerSpace->GetPhysicalTexture() : nullptr;
	}
	return nullptr;
}

FRHIShaderResourceView* FAllocatedVirtualTexture::GetPhysicalTextureView(uint32 InLayerIndex, bool bSRGB) const
{
	if (InLayerIndex < Description.NumLayers)
	{
		const FVirtualTexturePhysicalSpace* LayerSpace = PhysicalSpace[InLayerIndex];
		return LayerSpace ? LayerSpace->GetPhysicalTextureView(bSRGB) : nullptr;
	}
	return nullptr;
}

uint32 FAllocatedVirtualTexture::GetPhysicalTextureSize(uint32 InLayerIndex) const
{
	if (InLayerIndex < Description.NumLayers)
	{
		const FVirtualTexturePhysicalSpace* LayerSpace = PhysicalSpace[InLayerIndex];
		return LayerSpace ? LayerSpace->GetTextureSize() : 0u;
	}
	return 0u;
}

static inline uint32 BitcastFloatToUInt32(float v)
{
	const union
	{
		float FloatValue;
		uint32 UIntValue;
	} u = { v };
	return u.UIntValue;
}

void FAllocatedVirtualTexture::GetPackedPageTableUniform(FUintVector4* OutUniform, bool bApplyBlockScale) const
{
	static const auto MaxAnisotropyCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MaxAnisotropy"));

	const uint32 vPageX = FMath::ReverseMortonCode2(VirtualAddress);
	const uint32 vPageY = FMath::ReverseMortonCode2(VirtualAddress >> 1);
	const uint32 vPageSize = GetVirtualTileSize();
	const uint32 PageBorderSize = GetTileBorderSize();
	const uint32 WidthInPages = GetWidthInTiles();
	const uint32 HeightInPages = GetHeightInTiles();
	const uint32 vPageTableMipBias = FMath::FloorLog2(vPageSize);

	const uint32 MaxAnisotropy = FMath::Clamp<int32>(MaxAnisotropyCVar->GetValueOnRenderThread(), 1, PageBorderSize);
	const uint32 MaxAnisotropyLog2 = FMath::FloorLog2(MaxAnisotropy);

	// make sure everything fits in the allocated number of bits
	checkSlow(vPageX < 4096u);
	checkSlow(vPageY < 4096u);
	checkSlow(vPageTableMipBias < 16u);
	checkSlow(MaxLevel < 16u);
	checkSlow(SpaceID < 16u);

	if (bApplyBlockScale)
	{
		OutUniform[0].X = BitcastFloatToUInt32(1.0f / (float)WidthInBlocks);
		OutUniform[0].Y = BitcastFloatToUInt32(1.0f / (float)HeightInBlocks);
	}
	else
	{
		OutUniform[0].X = BitcastFloatToUInt32(1.0f);
		OutUniform[0].Y = BitcastFloatToUInt32(1.0f);
	}

	OutUniform[0].Z = BitcastFloatToUInt32((float)WidthInPages);
	OutUniform[0].W = BitcastFloatToUInt32((float)HeightInPages);

	OutUniform[1].X = BitcastFloatToUInt32((float)MaxAnisotropyLog2);
	OutUniform[1].Y = vPageX | (vPageY << 12) | (vPageTableMipBias << 24);
	OutUniform[1].Z = MaxLevel;
	OutUniform[1].W = (SpaceID << 28);
}

void FAllocatedVirtualTexture::GetPackedUniform(FUintVector4* OutUniform, uint32 LayerIndex) const
{
	const uint32 PhysicalTextureSize = GetPhysicalTextureSize(LayerIndex);
	if (PhysicalTextureSize > 0u)
	{
		const uint32 vPageSize = GetVirtualTileSize();
		const uint32 PageBorderSize = GetTileBorderSize();

		const float RcpPhysicalTextureSize = 1.0f / float(PhysicalTextureSize);
		const uint32 pPageSize = vPageSize + PageBorderSize * 2u;
		OutUniform->X = 0u;
		OutUniform->Y = BitcastFloatToUInt32((float)vPageSize * RcpPhysicalTextureSize);
		OutUniform->Z = BitcastFloatToUInt32((float)PageBorderSize * RcpPhysicalTextureSize);
		OutUniform->W = BitcastFloatToUInt32((float)pPageSize * RcpPhysicalTextureSize);
	}
	else
	{
		OutUniform->X = 0u;
		OutUniform->Y = 0u;
		OutUniform->Z = 0u;
		OutUniform->W = 0u;
	}
}
