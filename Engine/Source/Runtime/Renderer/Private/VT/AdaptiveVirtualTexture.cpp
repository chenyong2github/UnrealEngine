// Copyright Epic Games, Inc. All Rights Reserved.

#include "AdaptiveVirtualTexture.h"

#include "VT/AllocatedVirtualTexture.h"
#include "VT/VirtualTexturePhysicalSpace.h"
#include "VT/VirtualTextureScalability.h"
#include "VT/VirtualTextureSpace.h"
#include "VT/VirtualTextureSystem.h"


static TAutoConsoleVariable<int32> CVarAVTMaxFreePerFrame(
	TEXT("r.VT.AVT.MaxFreePerFrame"),
	1,
	TEXT("Max number of allocated VT for adaptive VT to free per frame"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarAVTMaxAllocPerFrame(
	TEXT("r.VT.AVT.MaxAllocPerFrame"),
	1,
	TEXT("Max number of allocated VT for adaptive VT to alloc per frame"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarAVTMaxPageResidency(
	TEXT("r.VT.AVT.MaxPageResidency"),
	75,
	TEXT("Percentage of page table to allocate before we start freeing to make space"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarAVTAgeToFree(
	TEXT("r.VT.AVT.AgeToFree"),
	300,
	TEXT("Number of frames for an allocation to be unused before it is considered for free"),
	ECVF_RenderThreadSafe
);


/**
 * IVirtualTexture implementation that redirects requests to another IVirtualTexture after having modified vLevel and vAddress.
 * Note that we expect vAddress values only in 32bit range from the VirtualTextureSystem, but we can expand into a genuine 64bit range here to feed our child producer.
 */
class FVirtualTextureAddressRedirect : public IVirtualTexture
{
public:
	FVirtualTextureAddressRedirect(FVirtualTextureProducerHandle InProducerHandle, IVirtualTexture* InVirtualTexture, FIntPoint InAddressOffset, int32 InLevelOffset)
		: ProducerHandle(InProducerHandle)
		, VirtualTexture(InVirtualTexture)
		, AddressOffset(InAddressOffset)
		, LevelOffset(InLevelOffset)
	{
	}

	virtual ~FVirtualTextureAddressRedirect()
	{
	}

	virtual FVTRequestPageResult RequestPageData(
		const FVirtualTextureProducerHandle& InProducerHandle,
		uint8 LayerMask,
		uint8 vLevel,
		uint64 vAddress,
		EVTRequestPagePriority Priority
	) override
	{
		uint64 X = FMath::ReverseMortonCode2_64(vAddress) + (AddressOffset.X >> (vLevel + LevelOffset));
		uint64 Y = FMath::ReverseMortonCode2_64(vAddress >> 1) + (AddressOffset.Y >> (vLevel + LevelOffset));
		vAddress = FMath::MortonCode2_64(X) | (FMath::MortonCode2_64(Y) << 1);
		vLevel = (uint8)(FMath::Max((int32)vLevel + LevelOffset, 0));

		return VirtualTexture->RequestPageData(ProducerHandle, LayerMask, vLevel, vAddress, Priority);
	}

	virtual IVirtualTextureFinalizer* ProducePageData(
		FRHICommandListImmediate& RHICmdList,
		ERHIFeatureLevel::Type FeatureLevel,
		EVTProducePageFlags Flags,
		const FVirtualTextureProducerHandle& InProducerHandle,
		uint8 LayerMask,
		uint8 vLevel,
		uint64 vAddress,
		uint64 RequestHandle,
		const FVTProduceTargetLayer* TargetLayers
	) override
	{
		uint64 X = FMath::ReverseMortonCode2_64(vAddress) + (AddressOffset.X >> (vLevel + LevelOffset));
		uint64 Y = FMath::ReverseMortonCode2_64(vAddress >> 1) + (AddressOffset.Y >> (vLevel + LevelOffset));
		vAddress = FMath::MortonCode2_64(X) | (FMath::MortonCode2_64(Y) << 1);
		vLevel = (uint8)(FMath::Max((int32)vLevel + LevelOffset, 0));

		return VirtualTexture->ProducePageData(RHICmdList, FeatureLevel, Flags, ProducerHandle, LayerMask, vLevel, vAddress, RequestHandle, TargetLayers);
	}

private:
	FVirtualTextureProducerHandle ProducerHandle;
	IVirtualTexture* VirtualTexture;
	FIntPoint AddressOffset;
	int32 LevelOffset;
};

/** Union to define the layout of our packed allocation requests. */
union FPackedAdaptiveAllocationRequest
{
	uint32 PackedValue = 0;
	struct
	{
		uint32 Space : 4;
		uint32 X : 12;
		uint32 Y : 12;
		uint32 bIsAllocated : 1;
		uint32 bIsRequest : 1;
		uint32 bIsValid : 1;
	};
};

/** Local helper functions. */
namespace
{
	IAllocatedVirtualTexture* CreateAllocatedVT(
		FVirtualTextureSystem* InSystem, 
		FAllocatedVTDescription const& InAllocatedDesc,
		FIntPoint InGridSize,
		uint8 InForcedSpaceID,
		int32 InWidthInTiles,
		int32 InHeightInTiles,
		FIntPoint InAddressOffset, 
		int32 InLevelOffset)
	{
		FAllocatedVTDescription AllocatedDesc = InAllocatedDesc;
	
		// We require bPrivateSpace since there can be only one adaptive VT per space.
		ensure(AllocatedDesc.bPrivateSpace); 
		AllocatedDesc.bPrivateSpace = true;
		AllocatedDesc.ForceSpaceID = InForcedSpaceID;
		AllocatedDesc.IndirectionTextureSize = FMath::Max(InGridSize.X, InGridSize.Y);

		for (int32 LayerIndex = 0; LayerIndex < InAllocatedDesc.NumTextureLayers; ++LayerIndex)
		{
			// Test if we have already written layer with a new handle.
			// If we have then we already processed this producer in an ealier layer and have nothing more to do.
			if (AllocatedDesc.ProducerHandle[LayerIndex] != InAllocatedDesc.ProducerHandle[LayerIndex])
			{
				continue;
			}

			FVirtualTextureProducerHandle ProducerHandle = InAllocatedDesc.ProducerHandle[LayerIndex];
			FVirtualTextureProducer* Producer = InSystem->FindProducer(ProducerHandle);
			FVTProducerDescription NewProducerDesc = Producer->GetDescription();
			NewProducerDesc.BlockWidthInTiles = InWidthInTiles;
			NewProducerDesc.BlockHeightInTiles = InHeightInTiles;
			NewProducerDesc.MaxLevel = FMath::CeilLogTwo(FMath::Max(InWidthInTiles, InHeightInTiles));

			IVirtualTexture* VirtualTextureProducer = Producer->GetVirtualTexture();
			IVirtualTexture* NewVirtualTextureProducer = new FVirtualTextureAddressRedirect(ProducerHandle, VirtualTextureProducer, InAddressOffset, InLevelOffset);
			FVirtualTextureProducerHandle NewProducerHandle = InSystem->RegisterProducer(NewProducerDesc, NewVirtualTextureProducer);

			// Copy new producer to all subsequent layers.
			for (int32 WriteLayerIndex = LayerIndex; WriteLayerIndex < InAllocatedDesc.NumTextureLayers; ++WriteLayerIndex)
			{
				if (InAllocatedDesc.ProducerHandle[WriteLayerIndex] == ProducerHandle)
				{
					AllocatedDesc.ProducerHandle[WriteLayerIndex] = NewProducerHandle;
				}
			}
		}

		return InSystem->AllocateVirtualTexture(AllocatedDesc);
	}

	static void RemapVT(FVirtualTextureSystem* InSystem, uint32 Frame, FAllocatedVirtualTexture* OldAllocatedVT, FAllocatedVirtualTexture* NewAllocatedVT)
	{
		const uint32 OldVirtualAddress = OldAllocatedVT->GetVirtualAddress();
		const uint32 NewVirtualAddress = NewAllocatedVT->GetVirtualAddress();

		check(OldAllocatedVT->GetSpaceID() == NewAllocatedVT->GetSpaceID());
		check(OldAllocatedVT->GetNumUniqueProducers() == NewAllocatedVT->GetNumUniqueProducers())

		for (uint32 ProducerIndex = 0u; ProducerIndex < OldAllocatedVT->GetNumUniqueProducers(); ++ProducerIndex)
		{
			check(OldAllocatedVT->GetUniqueProducerMipBias(ProducerIndex) == 0);
			check(NewAllocatedVT->GetUniqueProducerMipBias(ProducerIndex) == 0);

			const FVirtualTextureProducerHandle& OldProducerHandle = OldAllocatedVT->GetUniqueProducerHandle(ProducerIndex);
			const FVirtualTextureProducerHandle& NewProducerHandle = NewAllocatedVT->GetUniqueProducerHandle(ProducerIndex);

			FVirtualTextureProducer* OldProducer = InSystem->FindProducer(OldProducerHandle);
			FVirtualTextureProducer* NewProducer = InSystem->FindProducer(NewProducerHandle);

			if (OldProducer->GetDescription().bPersistentHighestMip)
			{
				InSystem->ForceUnlockAllTiles(OldProducerHandle, OldProducer);
			}

			const int32 vLevelBias = (int32)NewProducer->GetMaxLevel() - (int32)OldProducer->GetMaxLevel();

			check(OldProducer->GetNumPhysicalGroups() == NewProducer->GetNumPhysicalGroups());
			for (uint32 PhysicalGroupIndex = 0u; PhysicalGroupIndex < OldProducer->GetNumPhysicalGroups(); ++PhysicalGroupIndex)
			{
				FVirtualTexturePhysicalSpace* OldPhysicalSpace = OldProducer->GetPhysicalSpaceForPhysicalGroup(PhysicalGroupIndex);
				FVirtualTexturePhysicalSpace* NewPhysicalSpace = NewProducer->GetPhysicalSpaceForPhysicalGroup(PhysicalGroupIndex);

				FTexturePagePool& OldPagePool = OldPhysicalSpace->GetPagePool();
				FTexturePagePool& NewPagePool = NewPhysicalSpace->GetPagePool();
				check(&OldPagePool == &NewPagePool);

				OldPagePool.RemapPages(InSystem, OldAllocatedVT->GetSpaceID(), OldPhysicalSpace, OldProducerHandle, OldVirtualAddress, NewProducerHandle, NewVirtualAddress, vLevelBias, Frame);
			}
		}
	}

	static void DestroyAllocatedVT(FVirtualTextureSystem* InSystem, IAllocatedVirtualTexture* AllocatedVT)
	{
		FAllocatedVTDescription const& Desc = AllocatedVT->GetDescription();
		TArray<FVirtualTextureProducerHandle, TInlineAllocator<8>> ProducersToRelease;
		for (int32 LayerIndex = 0; LayerIndex < Desc.NumTextureLayers; ++LayerIndex)
		{
			ProducersToRelease.AddUnique(Desc.ProducerHandle[LayerIndex]);
		}
		InSystem->DestroyVirtualTexture(AllocatedVT);
		for (int32 ProducerIndex = 0; ProducerIndex < ProducersToRelease.Num(); ++ProducerIndex)
		{
			InSystem->ReleaseProducer(ProducersToRelease[ProducerIndex]);
		}
	}
}


FAdaptiveVirtualTexture::FAdaptiveVirtualTexture(
	FAdaptiveVTDescription const& InAdaptiveDesc,
	FAllocatedVTDescription const& InAllocatedDesc)
	: AdaptiveDesc(InAdaptiveDesc)
	, AllocatedDesc(InAllocatedDesc)
	, AllocatedVirtualTextureLowMips(nullptr)
	, NumAllocated(0)
{
	MaxLevel = FMath::Max(FMath::CeilLogTwo(AdaptiveDesc.TileCountX), FMath::CeilLogTwo(AdaptiveDesc.TileCountY));

	const int32 AdaptiveGridLevelsX = (int32)FMath::CeilLogTwo(AdaptiveDesc.TileCountX) - AdaptiveDesc.MaxAdaptiveLevel;
	const int32 AdaptiveGridLevelsY = (int32)FMath::CeilLogTwo(AdaptiveDesc.TileCountY) - AdaptiveDesc.MaxAdaptiveLevel;
	ensure(AdaptiveGridLevelsX >= 0 && AdaptiveGridLevelsY >= 0); // Aspect ratio is too big for desired grid size. This will give bad results.

	GridSize = FIntPoint(1 << FMath::Max(AdaptiveGridLevelsX, 0), 1 << FMath::Max(AdaptiveGridLevelsY, 0));

	// Prepare grid for adaptive allocations.
	AllocatedVirtualTextureGrid.AddZeroed(GridSize.X * GridSize.Y);
}

void FAdaptiveVirtualTexture::Init(FVirtualTextureSystem* InSystem)
{
	// Allocate a low mips virtual texture.
	const int32 LevelOffset = AdaptiveDesc.MaxAdaptiveLevel;
	AllocatedVirtualTextureLowMips = (FAllocatedVirtualTexture*)CreateAllocatedVT(InSystem, AllocatedDesc, GridSize, 0xff, GridSize.X, GridSize.Y, FIntPoint::ZeroValue, LevelOffset);
}

void FAdaptiveVirtualTexture::Destroy(FVirtualTextureSystem* InSystem)
{
	DestroyAllocatedVT(InSystem, AllocatedVirtualTextureLowMips);
	
	for (IAllocatedVirtualTexture* AllocatedVT : AllocatedVirtualTextureGrid)
	{
		if (AllocatedVT != nullptr)
		{
			DestroyAllocatedVT(InSystem, AllocatedVT);
		}
	}

	delete this;
}

IAllocatedVirtualTexture* FAdaptiveVirtualTexture::GetAllocatedVirtualTexture()
{
	return AllocatedVirtualTextureLowMips;
}

int32 FAdaptiveVirtualTexture::GetSpaceID() const
{
	return AllocatedVirtualTextureLowMips->GetSpaceID();
}

uint32 FAdaptiveVirtualTexture::GetPackedAllocationRequest(uint32 vAddress, uint32 vLevelPlusOne, uint32 Frame) const
{
	FPackedAdaptiveAllocationRequest Request;
	Request.Space = GetSpaceID();
	Request.bIsRequest = vLevelPlusOne == 0 ? 1 : 0;
	Request.bIsValid = 1;

	uint32 vAddressLocal;
	FAllocatedVirtualTexture* AllocatedVT = FVirtualTextureSystem::Get().GetSpace(GetSpaceID())->GetAllocator().Find(vAddress, vAddressLocal);
	
	if (AllocatedVT == nullptr)
	{
		// Requests are processed a few frames after the GPU requested. It's possible that the VT is no longer allocated.
		return 0; 
	}
	else if (AllocatedVT->GetFrameAllocated() > Frame - 3)
	{
		// Don't process any request for a virtual texture that was allocated in the last few frames.
		return 0;
	}
	else if (AllocatedVT == AllocatedVirtualTextureLowMips)
	{
		// Request comes from the low mips allocated VT.
		Request.X = FMath::ReverseMortonCode2(vAddressLocal);
		Request.Y = FMath::ReverseMortonCode2(vAddressLocal >> 1);
		Request.bIsAllocated = 0;
		
		int32 GridIndex = Request.X + Request.Y * GridSize.X;
		if (AllocatedVirtualTextureGrid[GridIndex] != nullptr)
		{
			// The higher mips are already allocated but this request came from the low res mips.
			// Do nothing, and if no higher mips are requested then eventually the allocated VT will be evicted.
			return 0;
		}
	}
	else
	{
		//todo[vt]: Store allocated VT in a faster lookup structure!
		int32 Index = AllocatedVirtualTextureGrid.Find(AllocatedVT);
		check (Index != INDEX_NONE);

		Request.X = Index % GridSize.X;
		Request.Y = Index / GridSize.X;
		Request.bIsAllocated = 1;

		// If we are allocated at the max level already then we don't want to request a new level.
		if (AllocatedVT->GetMaxLevel() >= AdaptiveDesc.MaxAdaptiveLevel)
		{
			Request.bIsRequest = 0;
		}
	}

	return Request.PackedValue;
}

void FAdaptiveVirtualTexture::QueuePackedAllocationRequests(FVirtualTextureSystem* InSystem, uint32 const* Requests, uint32 NumRequests, uint32 Frame)
{
	//todo[vt]: Sort and batch allocation requests.
	for (uint32 RequestIndex = 0; RequestIndex < NumRequests; ++RequestIndex)
	{
		FPackedAdaptiveAllocationRequest Request;
		Request.PackedValue = Requests[RequestIndex];

		FAdaptiveVirtualTexture* AdaptiveVT = InSystem->GetAdaptiveVirtualTexture(Request.Space);
		AdaptiveVT->QueuePackedAllocationRequests(Requests + RequestIndex, 1, Frame);
	}
}

void FAdaptiveVirtualTexture::QueuePackedAllocationRequests(uint32 const* Requests, uint32 NumRequests, uint32 Frame)
{
	for (uint32 RequestIndex = 0; RequestIndex < NumRequests; ++RequestIndex)
	{
		FPackedAdaptiveAllocationRequest Request;
		Request.PackedValue = Requests[RequestIndex];

		if (Request.bIsAllocated)
		{
			// Already allocated so mark as used. Do this bfore we process any requests to ensure we don't free before allocating.
			uint32 GridIndex = Request.X + Request.Y * GridSize.X;
			uint32 MaxVTLevel = AllocatedVirtualTextureGrid[GridIndex]->GetMaxLevel();
			uint32 Key = (Frame << 4) | MaxVTLevel;
			FreeHeap.Update(Key, GridIndex);
		}

		if (Request.bIsRequest)
		{
			// Store request to handle in UpdateAllocations()
			RequestsToMap.AddUnique(Request.PackedValue);
		}
	}
}

void FAdaptiveVirtualTexture::Free(FVirtualTextureSystem* InSystem, int32 GridIndex, uint32 Frame)
{
	FAllocatedVirtualTexture* OldAllocatedVT = AllocatedVirtualTextureGrid[GridIndex];
	DestroyAllocatedVT(InSystem, OldAllocatedVT);
	AllocatedVirtualTextureGrid[GridIndex] = nullptr;
	NumAllocated--;
	check(NumAllocated >= 0);

	// Update indirection texture
	//todo[vt]: Batch texture updates.
	FVirtualTextureSpace* Space = InSystem->GetSpace(GetSpaceID());
	FRHITexture* Texture = Space->GetPageTableIndirectionTexture();
	const uint32 X = GridIndex % GridSize.X;
	const uint32 Y = GridIndex / GridSize.X;
	const FUpdateTextureRegion2D Region(X, Y, 0, 0, 1, 1);
	const uint32 PackedIndirectionValue = 0;
	RHIUpdateTexture2D((FRHITexture2D*)Texture, 0, Region, 4, (uint8*)&PackedIndirectionValue);
}

bool FAdaptiveVirtualTexture::FreeLRU(FVirtualTextureSystem* InSystem, uint32 Frame, uint32 FrameAgeToFree)
{
	FVirtualTextureSpace* Space = InSystem->GetSpace(GetSpaceID());

	const uint32 GridIndex = FreeHeap.Top();
	const uint32 X = GridIndex % GridSize.X;
	const uint32 Y = GridIndex / GridSize.X;

	const uint32 Key = FreeHeap.GetKey(GridIndex);
	const uint32 LastFrameUsed = Key >> 4;
	if (LastFrameUsed + FrameAgeToFree > Frame)
	{
		return false;
	}

	FAllocatedVirtualTexture* OldAllocatedVT = AllocatedVirtualTextureGrid[GridIndex];
	check(OldAllocatedVT != nullptr);
	const uint32 CurrentLevel = OldAllocatedVT->GetMaxLevel();
	int32 NewLevel = CurrentLevel - 1;
	while (NewLevel > 0)
	{
		if (Space->GetPageTableSize() < Space->GetDescription().MaxSpaceSize || Space->GetAllocator().TryAlloc(NewLevel))
		{
			break;
		}
		--NewLevel;
	}

	if (NewLevel < 1)
	{
		FreeHeap.Pop();
		Free(InSystem, GridIndex, Frame);
	}
	else
	{
		Reallocate(InSystem, GridIndex, NewLevel, Frame);
	}

	return true;
}

void FAdaptiveVirtualTexture::Reallocate(FVirtualTextureSystem* InSystem, int32 GridIndex, int32 NewLevel, uint32 Frame)
{
	const uint32 X = GridIndex % GridSize.X;
	const uint32 Y = GridIndex / GridSize.X;
	const FIntPoint PageOffset(X * AdaptiveDesc.TileCountX / GridSize.X, Y * AdaptiveDesc.TileCountY / GridSize.Y);
	const int32 LevelOffset = (int32)AdaptiveDesc.MaxAdaptiveLevel - NewLevel;

	FAllocatedVirtualTexture* OldAllocatedVT = AllocatedVirtualTextureGrid[GridIndex];
	FAllocatedVirtualTexture* NewAllocatedVT = (FAllocatedVirtualTexture*)CreateAllocatedVT(InSystem, AllocatedDesc, GridSize, GetSpaceID(), 1 << NewLevel, 1 << NewLevel, PageOffset, LevelOffset);
	if (OldAllocatedVT != nullptr)
	{
		RemapVT(InSystem, Frame, OldAllocatedVT, NewAllocatedVT);
		DestroyAllocatedVT(InSystem, OldAllocatedVT);
		const uint32 Key = (Frame << 4) | NewLevel;
		FreeHeap.Update(Key, GridIndex);
	}
	else
	{
		const uint32 Key = (Frame << 4) | NewLevel;
		FreeHeap.Add(Key, GridIndex);
		NumAllocated++;
	}
	AllocatedVirtualTextureGrid[GridIndex] = NewAllocatedVT;

	// Update indirection texture
	//todo[vt]: Batch texture updates.
	FVirtualTextureSpace* Space = InSystem->GetSpace(GetSpaceID());
	FRHITexture* Texture = Space->GetPageTableIndirectionTexture();

	const uint32 vAddress = NewAllocatedVT->GetVirtualAddress();
	const uint32 vAddressX = FMath::ReverseMortonCode2(vAddress);
	const uint32 vAddressY = FMath::ReverseMortonCode2(vAddress >> 1);
	const uint32 PackedIndirectionValue = (1 << 28) | (NewLevel << 24) | (vAddressY << 12) | vAddressX;

	FUpdateTextureRegion2D Region(X, Y, 0, 0, 1, 1);
	RHIUpdateTexture2D((FRHITexture2D*)Texture, 0, Region, 4, (uint8*)&PackedIndirectionValue);
}

void FAdaptiveVirtualTexture::Allocate(FVirtualTextureSystem* InSystem, uint32 PackedRequest, uint32 Frame)
{
	FPackedAdaptiveAllocationRequest Request;
	Request.PackedValue = PackedRequest;

	uint32 X = Request.X;
	uint32 Y = Request.Y;
	uint32 GridIndex = Y * GridSize.X + X;

	FAllocatedVirtualTexture* OldAllocatedVT = AllocatedVirtualTextureGrid[GridIndex];
	check(Request.bIsAllocated || OldAllocatedVT == nullptr);
	uint32 CurrentMaxLevel = OldAllocatedVT ? OldAllocatedVT->GetMaxLevel() : 0;
	uint32 NewLevel = OldAllocatedVT ? OldAllocatedVT->GetMaxLevel() + 3 : 4;
	NewLevel = FMath::Min(NewLevel, AdaptiveDesc.MaxAdaptiveLevel);
	check(NewLevel > CurrentMaxLevel);

	FVirtualTextureSpace* Space = InSystem->GetSpace(GetSpaceID());
	if (Space->GetPageTableSize() >= Space->GetDescription().MaxSpaceSize && !Space->GetAllocator().TryAlloc(NewLevel))
	{
		// No space to alloc. Hopefully we can alloc next frame.
		return;
	}

	Reallocate(InSystem, GridIndex, NewLevel, Frame);
}

void FAdaptiveVirtualTexture::UpdateAllocations(FVirtualTextureSystem* InSystem, uint32 Frame)
{
	if (RequestsToMap.Num() == 0)
	{
		// Free old unused pages if there is no other work to do.
		const uint32 FrameAgeToFree = CVarAVTAgeToFree.GetValueOnRenderThread();
		const int32 NumToFree = FMath::Min(NumAllocated, CVarAVTMaxFreePerFrame.GetValueOnRenderThread());
			
		bool bFreeSuccess = true;
		for (int32 FreeCount = 0; bFreeSuccess && FreeCount < NumToFree; FreeCount++)
		{
			bFreeSuccess = FreeLRU(InSystem, Frame, FrameAgeToFree);
		}
	}
	else
	{
		// Free to keep within residency threshold.
		FVirtualTextureSpace* Space = InSystem->GetSpace(GetSpaceID());
		const uint32 TotalPages = Space->GetDescription().MaxSpaceSize * Space->GetDescription().MaxSpaceSize;
		const uint32 ResidencyPercent = FMath::Clamp(CVarAVTMaxPageResidency.GetValueOnRenderThread(), 10, 95);
		const uint32 TargetPages = TotalPages * ResidencyPercent / 100;
		const int32 NumToFree = FMath::Min(NumAllocated, CVarAVTMaxFreePerFrame.GetValueOnRenderThread());

		bool bFreeSuccess = true;
		for (int32 FreeCount = 0; bFreeSuccess && FreeCount < NumToFree && Space->GetAllocator().GetNumAllocatedPages() > TargetPages; FreeCount++)
		{
			const uint32 FrameAgeToFree = 15; // Hardcoded threshold. Don't release anything used more recently then this.
			bFreeSuccess = FreeLRU(InSystem, Frame, FrameAgeToFree);
		}

		// Process allocation requests.
		const int32 NumToAlloc = CVarAVTMaxAllocPerFrame.GetValueOnRenderThread();

		for (int32 AllocCount = 0; AllocCount < NumToAlloc && RequestsToMap.Num(); AllocCount++)
		{
			// Randomize request order to prevent feedback from top of the view being prioritized.
			int32 RequestIndex = FMath::Rand() % RequestsToMap.Num();
			uint32 PackedRequest = RequestsToMap[RequestIndex];
			Allocate(InSystem, PackedRequest, Frame);
			RequestsToMap.RemoveAtSwap(RequestIndex, 1, false);
		}
	}

	// Clear requests
	RequestsToMap.Reset();
}
