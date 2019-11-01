// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PackedNormal.h"
#include "Lumin/CAPIShims/LuminAPIHandMeshing.h"
#include "MRMeshComponent.h"
#include "AppEventHandler.h"

class FMagicLeapMeshTracker
{
public:
	FMagicLeapMeshTracker();
	virtual ~FMagicLeapMeshTracker() {}

	void OnClear();

public:
	// Keep a copy of the mesh data here.  MRMeshComponent will use it from the game and render thread.
	struct FMLCachedMeshData
	{
		typedef TSharedPtr<FMLCachedMeshData, ESPMode::ThreadSafe> SharedPtr;

		FMagicLeapMeshTracker* Owner = nullptr;

		FGuid BlockID;
		TArray<FVector> OffsetVertices;
		TArray<FVector> WorldVertices;
		TArray<uint32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UV0;
		TArray<FColor> VertexColors;
		TArray<FPackedNormal> Tangents;
		TArray<float> Confidence;

		void Recycle(SharedPtr& MeshData)
		{
			check(Owner);
			FMagicLeapMeshTracker* TempOwner = Owner;
			Owner = nullptr;

			BlockID.Invalidate();
			OffsetVertices.Reset();
			WorldVertices.Reset();
			Triangles.Reset();
			Normals.Reset();
			UV0.Reset();
			VertexColors.Reset();
			Tangents.Reset();
			Confidence.Reset();

			TempOwner->FreeMeshDataCache(MeshData);
		}

		void Init(FMagicLeapMeshTracker* InOwner)
		{
			check(!Owner);
			Owner = InOwner;
		}
	};

	// Pending MR Mesh bricks, generated from mesh update messages, which will be amortized
	// across frames to avoid spiking
	TMap<FGuid, FMLCachedMeshData::SharedPtr> PendingMeshBricks;

	FDelegateHandle OnClearDelegateHandle;

	// This receipt will be kept in the FSendBrickDataArgs to ensure the cached data outlives MRMeshComponent use of it.
	class FMeshTrackerBrickDataReceipt : public IMRMesh::FBrickDataReceipt
	{
	public:
		FMeshTrackerBrickDataReceipt(FMLCachedMeshData::SharedPtr& MeshData) : CachedMeshData(MeshData)
		{
		}
		~FMeshTrackerBrickDataReceipt() override
		{
			CachedMeshData->Recycle(CachedMeshData);
		}
	private:
		FMLCachedMeshData::SharedPtr CachedMeshData;
	};

	FMLCachedMeshData::SharedPtr AquireMeshDataCache()
	{
		if (FreeCachedMeshDatas.Num() > 0)
		{
			FScopeLock ScopeLock(&FreeCachedMeshDatasMutex);
			FMLCachedMeshData::SharedPtr CachedMeshData(FreeCachedMeshDatas.Pop(false));
			CachedMeshData->Init(this);
			return CachedMeshData;
		}
		else
		{
			FMLCachedMeshData::SharedPtr CachedMeshData(new FMLCachedMeshData());
			CachedMeshData->Init(this);
			CachedMeshDatas.Add(CachedMeshData);
			return CachedMeshData;
		}
	}

	void FreeMeshDataCache(FMLCachedMeshData::SharedPtr& DataCache);
	bool Create();
	bool Destroy();
	bool ConnectMRMesh(UMRMeshComponent* InMRMeshPtr);
	bool DisconnectMRMesh(class UMRMeshComponent* InMRMeshPtr);
	bool Update();
	bool RequestMesh();
	bool GetMeshResult();
	uint64 *GetBrickInfo(const FGuid& meshGuid, bool addIfNotFound);
	bool HasMRMesh() const;
	bool HasClient() const;

private:
	MagicLeap::IAppEventHandler AppEventHandler;
#if WITH_MLSDK
	// Handle to ML mesh tracker
	MLHandle MeshTracker;
	MLHandle CurrentMeshRequest;
#endif //WITH_MLSDK
	bool bCreating;
	// Maps system 128-bit mesh GUIDs to 64-bit brick indices
	TMap<FGuid, uint64> MeshBrickCache;
	uint64 MeshBrickIndex;
	class UMRMeshComponent* MRMesh;
	// A free list to recycle the CachedMeshData instances.
	FMLCachedMeshData::SharedPtr CurrentMeshDataCache;
	TArray<FMLCachedMeshData::SharedPtr> CachedMeshDatas;
	TArray<FMLCachedMeshData::SharedPtr> FreeCachedMeshDatas;
	FCriticalSection FreeCachedMeshDatasMutex; //The free list may be pushed/popped from multiple threads.
};
