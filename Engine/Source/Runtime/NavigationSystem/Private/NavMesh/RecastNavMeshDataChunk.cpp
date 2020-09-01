// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavMesh/RecastNavMeshDataChunk.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/PImplRecastNavMesh.h"
#include "NavMesh/RecastHelpers.h"
#include "NavMesh/RecastVersion.h"
#include "Detour/DetourNavMeshBuilder.h"

//----------------------------------------------------------------------//
// FRecastTileData                                                                
//----------------------------------------------------------------------//
FRecastTileData::FRawData::FRawData(uint8* InData)
	: RawData(InData)
{
}

FRecastTileData::FRawData::~FRawData()
{
#if WITH_RECAST
	dtFree(RawData);
#else
	FMemory::Free(RawData);
#endif
}

FRecastTileData::FRecastTileData()
	: OriginalX(0)
	, OriginalY(0)
	, X(0)
	, Y(0)
	, Layer(0)
	, TileDataSize(0)
	, TileCacheDataSize(0)
	, bAttached(false)
{
}

FRecastTileData::FRecastTileData(int32 DataSize, uint8* RawData, int32 CacheDataSize, uint8* CacheRawData)
	: OriginalX(0)
	, OriginalY(0)
	, X(0)
	, Y(0)
	, Layer(0)
	, TileDataSize(DataSize)
	, TileCacheDataSize(CacheDataSize)
	, bAttached(false)
{
	TileRawData = MakeShareable(new FRawData(RawData));
	TileCacheRawData = MakeShareable(new FRawData(CacheRawData));
}

// Helper to duplicate recast raw data
static uint8* DuplicateRecastRawData(uint8* Src, int32 SrcSize)
{
#if WITH_RECAST	
	uint8* DupData = (uint8*)dtAlloc(SrcSize, DT_ALLOC_PERM);
#else
	uint8* DupData = (uint8*)FMemory::Malloc(SrcSize);
#endif
	FMemory::Memcpy(DupData, Src, SrcSize);
	return DupData;
}

//----------------------------------------------------------------------//
// URecastNavMeshDataChunk                                                                
//----------------------------------------------------------------------//
URecastNavMeshDataChunk::URecastNavMeshDataChunk(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void URecastNavMeshDataChunk::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	int32 NavMeshVersion = NAVMESHVER_LATEST;
	Ar << NavMeshVersion;

	// when writing, write a zero here for now.  will come back and fill it in later.
	int64 RecastNavMeshSizeBytes = 0;
	int64 RecastNavMeshSizePos = Ar.Tell();
	Ar << RecastNavMeshSizeBytes;

	if (Ar.IsLoading())
	{
		if (NavMeshVersion < NAVMESHVER_MIN_COMPATIBLE)
		{
			// incompatible, just skip over this data.  navmesh needs rebuilt.
			Ar.Seek(RecastNavMeshSizePos + RecastNavMeshSizeBytes);
		}
#if WITH_RECAST
		else if (RecastNavMeshSizeBytes > 4)
		{
			SerializeRecastData(Ar, NavMeshVersion);
		}
#endif// WITH_RECAST
		else
		{
			// empty, just skip over this data
			Ar.Seek(RecastNavMeshSizePos + RecastNavMeshSizeBytes);
		}
	}
	else if (Ar.IsSaving())
	{
#if WITH_RECAST
		SerializeRecastData(Ar, NavMeshVersion);
#endif// WITH_RECAST

		int64 CurPos = Ar.Tell();
		RecastNavMeshSizeBytes = CurPos - RecastNavMeshSizePos;
		Ar.Seek(RecastNavMeshSizePos);
		Ar << RecastNavMeshSizeBytes;
		Ar.Seek(CurPos);
	}
}

#if WITH_RECAST
void URecastNavMeshDataChunk::SerializeRecastData(FArchive& Ar, int32 NavMeshVersion)
{
	int32 TileNum = Tiles.Num();
	Ar << TileNum;

	if (Ar.IsLoading())
	{
		Tiles.Empty(TileNum);
		for (int32 TileIdx = 0; TileIdx < TileNum; TileIdx++)
		{
			int32 TileDataSize = 0;
			Ar << TileDataSize;

			// Load tile data 
			uint8* TileRawData = nullptr;
			FPImplRecastNavMesh::SerializeRecastMeshTile(Ar, NavMeshVersion, TileRawData, TileDataSize); //allocates TileRawData on load
			
			if (TileRawData != nullptr)
			{
				// Load compressed tile cache layer
				int32 TileCacheDataSize = 0;
				uint8* TileCacheRawData = nullptr;
				if (Ar.UE4Ver() >= VER_UE4_ADD_MODIFIERS_RUNTIME_GENERATION && 
					(Ar.EngineVer().GetMajor() != 4 || Ar.EngineVer().GetMinor() != 7)) // Merged package from 4.7 branch
				{
					FPImplRecastNavMesh::SerializeCompressedTileCacheData(Ar, NavMeshVersion, TileCacheRawData, TileCacheDataSize); //allocates TileCacheRawData on load
				}
				
				// We are owner of tile raw data
				FRecastTileData TileData(TileDataSize, TileRawData, TileCacheDataSize, TileCacheRawData);
				Tiles.Add(TileData);
			}
		}
	}
	else if (Ar.IsSaving())
	{
		for (FRecastTileData& TileData : Tiles)
		{
			if (TileData.TileRawData.IsValid())
			{
				// Save tile itself
				Ar << TileData.TileDataSize;
				FPImplRecastNavMesh::SerializeRecastMeshTile(Ar, NavMeshVersion, TileData.TileRawData->RawData, TileData.TileDataSize);
				// Save compressed tile cache layer
				FPImplRecastNavMesh::SerializeCompressedTileCacheData(Ar, NavMeshVersion, TileData.TileCacheRawData->RawData, TileData.TileCacheDataSize);
			}
		}
	}
}
#endif// WITH_RECAST

TArray<uint32> URecastNavMeshDataChunk::AttachTiles(FPImplRecastNavMesh& NavMeshImpl)
{
	check(NavMeshImpl.NavMeshOwner && NavMeshImpl.NavMeshOwner->GetWorld());
	const bool bIsGameWorld = NavMeshImpl.NavMeshOwner->GetWorld()->IsGameWorld();

	// In editor we still need to own the data so a copy will be made.
	const bool bKeepCopyOfData = !bIsGameWorld;
	const bool bKeepCopyOfCacheData = !bIsGameWorld;

	return AttachTiles(NavMeshImpl, bKeepCopyOfData, bKeepCopyOfCacheData);
}

TArray<uint32> URecastNavMeshDataChunk::AttachTiles(FPImplRecastNavMesh& NavMeshImpl, const bool bKeepCopyOfData, const bool bKeepCopyOfCacheData)
{
	TArray<uint32> Result;
	Result.Reserve(Tiles.Num());

#if WITH_RECAST	
	dtNavMesh* NavMesh = NavMeshImpl.DetourNavMesh;

	if (NavMesh != nullptr)
	{
		for (FRecastTileData& TileData : Tiles)
		{
			if (!TileData.bAttached && TileData.TileRawData.IsValid())
			{
				// Attach mesh tile to target nav mesh 
				dtTileRef TileRef = 0;
				const dtMeshTile* MeshTile = nullptr;

				dtStatus status = NavMesh->addTile(TileData.TileRawData->RawData, TileData.TileDataSize, DT_TILE_FREE_DATA, 0, &TileRef);
				if (dtStatusFailed(status))
				{
					continue;
				}
				else
				{
					MeshTile = NavMesh->getTileByRef(TileRef);
					check(MeshTile);
					
					TileData.X = MeshTile->header->x;
					TileData.Y = MeshTile->header->y;
					TileData.Layer = MeshTile->header->layer;
					TileData.bAttached = true;
				}

				if (bKeepCopyOfData == false)
				{
					// We don't own tile data anymore it will be released by recast navmesh 
					TileData.TileDataSize = 0;
					TileData.TileRawData->RawData = nullptr;
				}
				else
				{
					// In the editor we still need to own data, so make a copy of it
					TileData.TileRawData->RawData = DuplicateRecastRawData(TileData.TileRawData->RawData, TileData.TileDataSize);
				}

				// Attach tile cache layer to target nav mesh
				if (TileData.TileCacheDataSize > 0)
				{
					FBox TileBBox = Recast2UnrealBox(MeshTile->header->bmin, MeshTile->header->bmax);

					FNavMeshTileData LayerData(TileData.TileCacheRawData->RawData, TileData.TileCacheDataSize, TileData.Layer, TileBBox);
					NavMeshImpl.AddTileCacheLayer(TileData.X, TileData.Y, TileData.Layer, LayerData);

					if (bKeepCopyOfCacheData == false)
					{
						// We don't own tile cache data anymore it will be released by navmesh
						TileData.TileCacheDataSize = 0;
						TileData.TileCacheRawData->RawData = nullptr;
					}
					else
					{
						// In the editor we still need to own data, so make a copy of it
						TileData.TileCacheRawData->RawData = DuplicateRecastRawData(TileData.TileCacheRawData->RawData, TileData.TileCacheDataSize);
					}
				}

				Result.Add(NavMesh->decodePolyIdTile(TileRef));
			}
		}
	}
#endif// WITH_RECAST

	UE_LOG(LogNavigation, Log, TEXT("Attached %d tiles to NavMesh - %s"), Result.Num(), *NavigationDataName.ToString());
	return Result;
}

TArray<uint32> URecastNavMeshDataChunk::DetachTiles(FPImplRecastNavMesh& NavMeshImpl)
{
	check(NavMeshImpl.NavMeshOwner && NavMeshImpl.NavMeshOwner->GetWorld());
	const bool bIsGameWorld = NavMeshImpl.NavMeshOwner->GetWorld()->IsGameWorld();

	// Keep data in game worlds (in editor we have a copy of the data so we don't keep it).
	const bool bTakeDataOwnership = bIsGameWorld;
	const bool bTakeCacheDataOwnership = bIsGameWorld;

	return DetachTiles(NavMeshImpl, bTakeDataOwnership, bTakeCacheDataOwnership);
}

TArray<uint32> URecastNavMeshDataChunk::DetachTiles(FPImplRecastNavMesh& NavMeshImpl, const bool bTakeDataOwnership, const bool bTakeCacheDataOwnership)
{
	TArray<uint32> Result;
	Result.Reserve(Tiles.Num());

#if WITH_RECAST
	dtNavMesh* NavMesh = NavMeshImpl.DetourNavMesh;

	if (NavMesh != nullptr)
	{
		for (FRecastTileData& TileData : Tiles)
		{
			if (TileData.bAttached)
			{
				// Detach tile cache layer and take ownership over compressed data
				dtTileRef TileRef = 0;
				const dtMeshTile* MeshTile = NavMesh->getTileAt(TileData.X, TileData.Y, TileData.Layer);
				if (MeshTile)
				{
					TileRef = NavMesh->getTileRef(MeshTile);

					if (bTakeCacheDataOwnership)
					{
						FNavMeshTileData TileCacheData = NavMeshImpl.GetTileCacheLayer(TileData.X, TileData.Y, TileData.Layer);
						if (TileCacheData.IsValid())
						{
							TileData.TileCacheDataSize = TileCacheData.DataSize;
							TileData.TileCacheRawData->RawData = TileCacheData.Release();
						}
					}
				
					NavMeshImpl.RemoveTileCacheLayer(TileData.X, TileData.Y, TileData.Layer);

					if (bTakeDataOwnership)
					{
						// Remove tile from navmesh and take ownership of tile raw data
						NavMesh->removeTile(TileRef, &TileData.TileRawData->RawData, &TileData.TileDataSize);
					}
					else
					{
						// In the editor we have a copy of tile data so just release tile in navmesh
						NavMesh->removeTile(TileRef, nullptr, nullptr);
					}
						
					Result.Add(NavMesh->decodePolyIdTile(TileRef));
				}
			}

			TileData.bAttached = false;
			TileData.X = 0;
			TileData.Y = 0;
			TileData.Layer = 0;
		}
	}
#endif// WITH_RECAST

	UE_LOG(LogNavigation, Log, TEXT("Detached %d tiles from NavMesh - %s"), Result.Num(), *NavigationDataName.ToString());
	return Result;
}

void URecastNavMeshDataChunk::MoveTiles(FPImplRecastNavMesh& NavMeshImpl, const FIntPoint& Offset, const float RotationDeg, const FVector2D& RotationCenter)
{
#if WITH_RECAST	
	UE_LOG(LogNavigation, Log, TEXT("%s Moving %i tiles on navmesh %s."), ANSI_TO_TCHAR(__FUNCTION__), Tiles.Num(), *NavigationDataName.ToString());

	dtNavMesh* NavMesh = NavMeshImpl.DetourNavMesh;
	if (NavMesh != nullptr)
	{
		for (FRecastTileData& TileData : Tiles)
		{
			if (TileData.TileCacheDataSize != 0)
			{
				UE_LOG(LogNavigation, Error, TEXT("   TileCacheRawData is expected to be empty. No support for moving the cache data yet."));
				continue;
			}

			if ((TileData.bAttached == false) && TileData.TileRawData.IsValid())
			{
				const FVector RcRotationCenter = Unreal2RecastPoint(FVector(RotationCenter.X, RotationCenter.Y, 0.f));

				const float TileWidth = NavMesh->getParams()->tileWidth;
				const float TileHeight = NavMesh->getParams()->tileHeight;

				const dtMeshHeader* Header = (dtMeshHeader*)TileData.TileRawData->RawData;
				if (Header->magic != DT_NAVMESH_MAGIC || Header->version != DT_NAVMESH_VERSION)
				{
					continue;
				}

				// Apply rotation to tile coordinates
				int DeltaX = 0;
				int DeltaY = 0;
				FBox TileBox(Recast2UnrealPoint(Header->bmin), Recast2UnrealPoint(Header->bmax));
				FVector RcTileCenter = Unreal2RecastPoint(TileBox.GetCenter());
				dtComputeTileOffsetFromRotation(&RcTileCenter.X, &RcRotationCenter.X, RotationDeg, TileWidth, TileHeight, DeltaX, DeltaY);

				const int OffsetWithRotX = Offset.X + DeltaX;
				const int OffsetWithRotY = Offset.Y + DeltaY;
				const bool bSuccess = dtTransformTileData(TileData.TileRawData->RawData, TileData.TileDataSize, OffsetWithRotX, OffsetWithRotY, TileWidth, TileHeight, RotationDeg);
				UE_CLOG(bSuccess, LogNavigation, Log, TEXT("   Moved tile from (%i,%i) to (%i,%i)."), TileData.OriginalX, TileData.OriginalY, (TileData.OriginalX + OffsetWithRotX), (TileData.OriginalY + OffsetWithRotY));
			}
		}
	}

	UE_LOG(LogNavigation, Log, TEXT("%s Moving done."), ANSI_TO_TCHAR(__FUNCTION__));
#endif// WITH_RECAST
}

int32 URecastNavMeshDataChunk::GetNumTiles() const
{
	return Tiles.Num();
}

void URecastNavMeshDataChunk::ReleaseTiles()
{
	Tiles.Reset();
}

void URecastNavMeshDataChunk::GatherTiles(const FPImplRecastNavMesh* NavMeshImpl, const TArray<int32>& TileIndices)
{
	const EGatherTilesCopyMode CopyMode = NavMeshImpl->NavMeshOwner->SupportsRuntimeGeneration() ? EGatherTilesCopyMode::CopyDataAndCacheData : EGatherTilesCopyMode::CopyData;
	GetTiles(NavMeshImpl, TileIndices, CopyMode);
}

void URecastNavMeshDataChunk::GetTiles(const FPImplRecastNavMesh* NavMeshImpl, const TArray<int32>& TileIndices, const EGatherTilesCopyMode CopyMode, const bool bMarkAsAttached /*= true*/)
{
	Tiles.Empty(TileIndices.Num());

	const dtNavMesh* NavMesh = NavMeshImpl->DetourNavMesh;
	
	for (int32 TileIdx : TileIndices)
	{
		const dtMeshTile* Tile = NavMesh->getTile(TileIdx);
		if (Tile && Tile->header)
		{
			// Make our own copy of tile data
			uint8* RawTileData = nullptr;
			if (CopyMode & EGatherTilesCopyMode::CopyData)
			{
				RawTileData = DuplicateRecastRawData(Tile->data, Tile->dataSize);
			}

			// We need tile cache data only if navmesh supports any kind of runtime generation
			FNavMeshTileData TileCacheData;
			uint8* RawTileCacheData = nullptr;
			if (CopyMode & EGatherTilesCopyMode::CopyCacheData)
			{
				TileCacheData = NavMeshImpl->GetTileCacheLayer(Tile->header->x, Tile->header->y, Tile->header->layer);
				if (TileCacheData.IsValid())
				{
					// Make our own copy of tile cache data
					RawTileCacheData = DuplicateRecastRawData(TileCacheData.GetData(), TileCacheData.DataSize);
				}
			}

			FRecastTileData RecastTileData(Tile->dataSize, RawTileData, TileCacheData.DataSize, RawTileCacheData);
			RecastTileData.OriginalX = Tile->header->x;
			RecastTileData.OriginalY = Tile->header->y;
			RecastTileData.X = Tile->header->x;
			RecastTileData.Y = Tile->header->y;
			RecastTileData.Layer = Tile->header->layer;
			RecastTileData.bAttached = bMarkAsAttached;

			Tiles.Add(RecastTileData);
		}
	}
}
