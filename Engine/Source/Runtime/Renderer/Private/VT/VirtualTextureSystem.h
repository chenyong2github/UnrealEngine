// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RendererInterface.h"
#include "Templates/UniquePtr.h"
#include "VT/VirtualTextureProducer.h"
#include "VT/TexturePageLocks.h"
#include "VirtualTexturing.h"

class FAdaptiveVirtualTexture;
class FAllocatedVirtualTexture;
class FScene;
class FUniquePageList;
class FUniqueRequestList;
class FVirtualTexturePhysicalSpace;
class FVirtualTextureProducer;
class FVirtualTextureSpace;
struct FVTSpaceDescription;
struct FVTPhysicalSpaceDescription;
union FPhysicalSpaceIDAndAddress;
struct FFeedbackAnalysisParameters;
struct FGatherRequestsParameters;
struct FPageUpdateBuffer;

extern uint32 GetTypeHash(const FAllocatedVTDescription& Description);

class FVirtualTextureSystem
{
public:
	static void Initialize();
	static void Shutdown();
	static FVirtualTextureSystem& Get();

	void AllocateResources(FRDGBuilder& GraphBuilder, ERHIFeatureLevel::Type FeatureLevel);
	void CallPendingCallbacks();
	void Update( FRDGBuilder& GraphBuilder, ERHIFeatureLevel::Type FeatureLevel, FScene* Scene);
	void ReleasePendingResources();

	IAllocatedVirtualTexture* AllocateVirtualTexture(const FAllocatedVTDescription& Desc);
	void DestroyVirtualTexture(IAllocatedVirtualTexture* AllocatedVT);
	void ReleaseVirtualTexture(FAllocatedVirtualTexture* AllocatedVT);
	void RemoveAllocatedVT(FAllocatedVirtualTexture* AllocatedVT);

	FVirtualTextureProducerHandle RegisterProducer(const FVTProducerDescription& InDesc, IVirtualTexture* InProducer);
	void ReleaseProducer(const FVirtualTextureProducerHandle& Handle);
	void AddProducerDestroyedCallback(const FVirtualTextureProducerHandle& Handle, FVTProducerDestroyedFunction* Function, void* Baton);
	uint32 RemoveAllProducerDestroyedCallbacks(const void* Baton);
	FVirtualTextureProducer* FindProducer(const FVirtualTextureProducerHandle& Handle);

	IAdaptiveVirtualTexture* AllocateAdaptiveVirtualTexture(const FAdaptiveVTDescription& AdaptiveVTDesc, const FAllocatedVTDescription& AllocatedVTDesc);
	void DestroyAdaptiveVirtualTexture(IAdaptiveVirtualTexture* AdaptiveVT);

	FVirtualTextureSpace* AcquireSpace(const FVTSpaceDescription& InDesc, uint8 InForceSpaceID, FAllocatedVirtualTexture* AllocatedVT);
	void ReleaseSpace(FVirtualTextureSpace* Space);

	FVirtualTexturePhysicalSpace* AcquirePhysicalSpace(const FVTPhysicalSpaceDescription& InDesc);

	FVirtualTextureSpace* GetSpace(uint8 ID) const { check(ID < MaxSpaces); return Spaces[ID].Get(); }
	FAdaptiveVirtualTexture* GetAdaptiveVirtualTexture(uint8 ID) const { check(ID < MaxSpaces); return AdaptiveVTs[ID]; }
	FVirtualTexturePhysicalSpace* GetPhysicalSpace(uint16 ID) const { check(PhysicalSpaces[ID]);  return PhysicalSpaces[ID]; }

	void LockTile(const FVirtualTextureLocalTile& Tile);
	void UnlockTile(const FVirtualTextureLocalTile& Tile, const FVirtualTextureProducer* Producer);
	void ForceUnlockAllTiles(const FVirtualTextureProducerHandle& ProducerHandle, const FVirtualTextureProducer* Producer);
	void RequestTiles(const FVector2D& InScreenSpaceSize, int32 InMipLevel = -1);
	void RequestTilesForRegion(const IAllocatedVirtualTexture* AllocatedVT, const FVector2D& InScreenSpaceSize, const FIntRect& InTextureRegion, int32 InMipLevel = -1);
	void LoadPendingTiles(FRDGBuilder& GraphBuilder, ERHIFeatureLevel::Type FeatureLevel);
	
	void FlushCache();
	void FlushCache(FVirtualTextureProducerHandle const& ProducerHandle, FIntRect const& TextureRegion, uint32 MaxLevel);

private:
	friend class FFeedbackAnalysisTask;
	friend class FGatherRequestsTask;

	FVirtualTextureSystem();
	~FVirtualTextureSystem();

	void DestroyPendingVirtualTextures();
	void ReleasePendingSpaces();

	void RequestTilesForRegionInternal(const IAllocatedVirtualTexture* AllocatedVT, const FIntRect& InTextureRegion, uint32 vLevel);
	
	void SubmitRequestsFromLocalTileList(TArray<FVirtualTextureLocalTile>& OutDeferredTiles, const TSet<FVirtualTextureLocalTile>& LocalTileList, EVTProducePageFlags Flags, FRDGBuilder& GraphBuilder, ERHIFeatureLevel::Type FeatureLevel);

	void SubmitPreMappedRequests(FRDGBuilder& GraphBuilder, ERHIFeatureLevel::Type FeatureLevel);

	void SubmitRequests(FRDGBuilder& GraphBuilder, ERHIFeatureLevel::Type FeatureLevel, FMemStack& MemStack, FUniqueRequestList* RequestList, bool bAsync);

	void GatherRequests(FUniqueRequestList* MergedRequestList, const FUniquePageList* UniquePageList, uint32 FrameRequested, FMemStack& MemStack);

	void AddPageUpdate(FPageUpdateBuffer* Buffers, uint32 FlushCount, uint32 PhysicalSpaceID, uint16 pAddress);

	void GatherRequestsTask(const FGatherRequestsParameters& Parameters);
	void FeedbackAnalysisTask(const FFeedbackAnalysisParameters& Parameters);

	void GetContinuousUpdatesToProduce(FUniqueRequestList const* RequestList, int32 MaxTilesToProduce);

	void UpdateCSVStats() const;

	uint32	Frame;

	static const uint32 MaxNumTasks = 16;
	static const uint32 MaxSpaces = 16;
	TUniquePtr<FVirtualTextureSpace> Spaces[MaxSpaces];
	TArray<FVirtualTexturePhysicalSpace*> PhysicalSpaces;
	FVirtualTextureProducerCollection Producers;

	FCriticalSection PendingDeleteLock;
	TArray<FAllocatedVirtualTexture*> PendingDeleteAllocatedVTs;

	TMap<FAllocatedVTDescription, FAllocatedVirtualTexture*> AllocatedVTs;

	FAdaptiveVirtualTexture* AdaptiveVTs[MaxSpaces] = { nullptr };

	bool bFlushCaches;
	void FlushCachesFromConsole();
	FAutoConsoleCommand FlushCachesCommand;

	void DumpFromConsole();
	FAutoConsoleCommand DumpCommand;

	void ListPhysicalPoolsFromConsole();
	FAutoConsoleCommand ListPhysicalPools;

#if WITH_EDITOR
	void SaveAllocatorImagesFromConsole();
	FAutoConsoleCommand SaveAllocatorImages;
#endif

	FCriticalSection RequestedTilesLock;
	TArray<uint32> RequestedPackedTiles;

	TArray<FVirtualTextureLocalTile> TilesToLock;
	FTexturePageLocks TileLocks;

	TSet<FVirtualTextureLocalTile> ContinuousUpdateTilesToProduce;
	TSet<FVirtualTextureLocalTile> MappedTilesToProduce;
	TArray<FVirtualTextureLocalTile> TransientCollectedPages;
	TArray<FAllocatedVirtualTexture*> AllocatedVTsToMap;
	TArray<IVirtualTextureFinalizer*> Finalizers;
};


