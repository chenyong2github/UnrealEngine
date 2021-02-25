// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceSkeletalMeshConnectivity.h"

#include "Algo/StableSort.h"
#include "Algo/Unique.h"
#include "NDISkeletalMeshCommon.h"
#include "NiagaraResourceArrayWriter.h"
#include "NiagaraSettings.h"
#include "NiagaraStats.h"

#include <limits>

FSkeletalMeshConnectivityHandle::FSkeletalMeshConnectivityHandle()
	: ConnectivityData(nullptr)
{
}

FSkeletalMeshConnectivityHandle::FSkeletalMeshConnectivityHandle(FSkeletalMeshConnectivityUsage InUsage, const TSharedPtr<FSkeletalMeshConnectivity>& InMappingData, bool bNeedsDataImmediately)
	: Usage(InUsage)
	, ConnectivityData(InMappingData)
{
	if (FSkeletalMeshConnectivity* MappingData = ConnectivityData.Get())
	{
		MappingData->RegisterUser(Usage, bNeedsDataImmediately);
	}
}

FSkeletalMeshConnectivityHandle::~FSkeletalMeshConnectivityHandle()
{
	if (FSkeletalMeshConnectivity* MappingData = ConnectivityData.Get())
	{
		MappingData->UnregisterUser(Usage);
	}
}

FSkeletalMeshConnectivityHandle::FSkeletalMeshConnectivityHandle(FSkeletalMeshConnectivityHandle&& Other)
{
	Usage = Other.Usage;
	ConnectivityData = Other.ConnectivityData;
	Other.ConnectivityData = nullptr;
}

FSkeletalMeshConnectivityHandle& FSkeletalMeshConnectivityHandle::operator=(FSkeletalMeshConnectivityHandle&& Other)
{
	if (this != &Other)
	{
		Usage = Other.Usage;
		ConnectivityData = Other.ConnectivityData;
		Other.ConnectivityData = nullptr;
	}
	return *this;
}

FSkeletalMeshConnectivityHandle::operator bool() const
{
	return ConnectivityData.IsValid();
}

int32 FSkeletalMeshConnectivityHandle::GetAdjacentTriangleIndex(int32 VertexIndex, int32 AdjacencyIndex) const
{
	if (ConnectivityData)
	{
		return ConnectivityData->GetAdjacentTriangleIndex(VertexIndex, AdjacencyIndex);
	}

	return INDEX_NONE;
}

const FSkeletalMeshConnectivityProxy* FSkeletalMeshConnectivityHandle::GetProxy() const
{
	if (ConnectivityData)
	{
		return ConnectivityData->GetProxy();
	}
	return nullptr;
}

FSkeletalMeshConnectivity::FSkeletalMeshConnectivity(TWeakObjectPtr<USkeletalMesh> InMeshObject, int32 InLodIndex)
	: LodIndex(InLodIndex)
	, MeshObject(InMeshObject)
	, GpuUserCount(0)
	, ReleasedByRT(false)
	, QueuedForRelease(false)
{
}

bool FSkeletalMeshConnectivity::IsUsed() const
{
	return (GpuUserCount > 0);
}

bool FSkeletalMeshConnectivity::CanBeDestroyed() const
{
	return !IsUsed() && (!QueuedForRelease || ReleasedByRT);
}

void FSkeletalMeshConnectivity::RegisterUser(FSkeletalMeshConnectivityUsage Usage, bool bNeedsDataImmediately)
{
	if (Usage.RequiresGpuAccess)
	{
		if (GpuUserCount++ == 0)
		{
			Proxy.Initialize(*this);
			BeginInitResource(&Proxy);
		}
	}
}

void FSkeletalMeshConnectivity::UnregisterUser(FSkeletalMeshConnectivityUsage Usage)
{
	if (Usage.RequiresGpuAccess)
	{
		if (--GpuUserCount == 0)
		{
			QueuedForRelease = true;
			ReleasedByRT = false;
			FThreadSafeBool* Released = &ReleasedByRT;

			BeginReleaseResource(&Proxy);

			ENQUEUE_RENDER_COMMAND(BeginDestroyCommand)(
				[Released](FRHICommandListImmediate& RHICmdList)
				{
					*Released = true;
				});
		}
	}
}

bool FSkeletalMeshConnectivity::CanBeUsed(const TWeakObjectPtr<USkeletalMesh>& InMeshObject, int32 InLodIndex) const
{
	return !QueuedForRelease && LodIndex == InLodIndex && MeshObject == InMeshObject;
}

bool FSkeletalMeshConnectivity::IsValidMeshObject(TWeakObjectPtr<USkeletalMesh>& MeshObject, int32 InLodIndex)
{
	USkeletalMesh* Mesh = MeshObject.Get();

	if (!Mesh)
	{
		return false;
	}

	const FSkeletalMeshLODInfo* LodInfo = Mesh->GetLODInfo(InLodIndex);
	if (!LodInfo)
	{
		// invalid Lod index
		return false;
	}

	if (!LodInfo->bAllowCPUAccess)
	{
		// we need CPU access to buffers in order to generate our UV mapping quad tree
		return false;
	}

	if (!GetLodRenderData(*Mesh, InLodIndex))
	{
		// no render data available
		return false;
	}

	return true;
}

int32 FSkeletalMeshConnectivity::GetAdjacentTriangleIndex(int32 VertexIndex, int32 AdjacencyIndex) const
{
	return INDEX_NONE;
}

void FSkeletalMeshConnectivity::GetTriangleVertices(int32 TriangleIndex, int32& OutVertex0, int32& OutVertex1, int32& OutVertex2) const
{
	OutVertex0 = INDEX_NONE;
	OutVertex1 = INDEX_NONE;
	OutVertex2 = INDEX_NONE;
}

const FSkeletalMeshLODRenderData* FSkeletalMeshConnectivity::GetLodRenderData(const USkeletalMesh& Mesh, int32 LodIndex)
{
	if (FSkeletalMeshRenderData* RenderData = Mesh.GetResourceForRendering())
	{
		if (RenderData->LODRenderData.IsValidIndex(LodIndex))
		{
			return &RenderData->LODRenderData[LodIndex];
		}
	}

	return nullptr;
}

const FSkeletalMeshLODRenderData* FSkeletalMeshConnectivity::GetLodRenderData() const
{
	if (const USkeletalMesh* Mesh = MeshObject.Get())
	{
		return GetLodRenderData(*Mesh, LodIndex);
	}

	return nullptr;
}

FString FSkeletalMeshConnectivity::GetMeshName() const
{
	if (const USkeletalMesh* Mesh = MeshObject.Get())
	{
		return Mesh->GetPathName();
	}
	return TEXT("<none>");
}

const FSkeletalMeshConnectivityProxy* FSkeletalMeshConnectivity::GetProxy() const
{
	return &Proxy;
}

struct FAdjacencyVertexOverlapKey
{
	FVector Position;
};

uint32 GetTypeHash(const FAdjacencyVertexOverlapKey& Key)
{
	return GetTypeHash(Key.Position);
}

bool operator==(const FAdjacencyVertexOverlapKey& Lhs, const FAdjacencyVertexOverlapKey& Rhs)
{
	return Lhs.Position == Rhs.Position;
}

template<typename TriangleIndexType, bool SortBySize>
static bool BuildAdjacencyBuffer(const FSkeletalMeshLODRenderData& LodRenderData, int32 MaxAdjacencyCount, TResourceArray<uint8>& Buffer, int32& MaxFoundAdjacentTriangleCount)
{
	const FRawStaticIndexBuffer16or32Interface* IndexBuffer = LodRenderData.MultiSizeIndexContainer.GetIndexBuffer();
	const FPositionVertexBuffer* VertexBuffer = SortBySize ? &LodRenderData.StaticVertexBuffers.PositionVertexBuffer : nullptr;
	const uint32 IndexCount = IndexBuffer->Num();
	const uint32 TriangleCount = IndexCount / 3;

	const TriangleIndexType MaxValidTriangleIndex = std::numeric_limits<TriangleIndexType>::max() - 1; // reserve -1 for an invalid index

	if (TriangleCount >= MaxValidTriangleIndex)
	{
		return false;
	}

	int32 MinVertexValue = std::numeric_limits<int32>::max();
	int32 MaxVertexValue = std::numeric_limits<int32>::min();

	int32 UniqueVertexCount = 0;
	TMultiMap<int32 /*UniqueVertexIndex*/, TriangleIndexType> RawAdjacency;
	TMap<FAdjacencyVertexOverlapKey, int32 /*UniqueVertexIndex*/> UniqueIndexMap;
	TMap<int32 /*VertexIndex*/, int32 /*UniqueVertexIndex*/> VertexToUniqueIndexMap;

	for (uint32 IndexIt = 0; IndexIt < IndexCount; ++IndexIt)
	{
		const TriangleIndexType TriangleId = IndexIt / 3;
		const int32 VertexId = IndexBuffer->Get(IndexIt);
		MinVertexValue = FMath::Min(MinVertexValue, VertexId);
		MaxVertexValue = FMath::Max(MaxVertexValue, VertexId);

		FAdjacencyVertexOverlapKey OverlapKey;
		OverlapKey.Position = VertexBuffer->VertexPosition(VertexId);

		int32 UniqueIndex = UniqueVertexCount;
		if (int32* ExistingGroup = UniqueIndexMap.Find(OverlapKey))
		{
			UniqueIndex = *ExistingGroup;
		}
		else
		{
			UniqueIndexMap.Add(OverlapKey, UniqueVertexCount);
			++UniqueVertexCount;
		}

		int32* ExistingUniqueIndex = VertexToUniqueIndexMap.Find(VertexId);
		if (!ExistingUniqueIndex)
		{
			VertexToUniqueIndexMap.Add(VertexId, UniqueIndex);
		}
		else
		{
			check(*ExistingUniqueIndex == UniqueIndex);
		}

		RawAdjacency.Add(UniqueIndex, TriangleId);
	}

	const int32 SizePerVertex = MaxAdjacencyCount * sizeof(TriangleIndexType);
	const int32 BufferSize = (MaxVertexValue - MinVertexValue) * SizePerVertex;
	const int32 PaddedBufferSize = 4 * FMath::DivideAndRoundUp(BufferSize, 4);
	Buffer.Init(0xFF, PaddedBufferSize);

	MaxFoundAdjacentTriangleCount = 0;

	{
		FNiagaraResourceArrayWriter Ar(Buffer);

		TArray<TriangleIndexType> TriangleValues;
		TArray<TriangleIndexType> SortedValues;
		TArray<float> TriangleSizes;
		TArray<int32> SortIndices;

		TriangleValues.Reserve(MaxAdjacencyCount);
		if (SortBySize)
		{
			TriangleSizes.Reserve(MaxAdjacencyCount);
			SortIndices.Reserve(MaxAdjacencyCount);
			SortedValues.Reserve(MaxAdjacencyCount);
		}

		for (int32 VertexIt = MinVertexValue; VertexIt <= MaxVertexValue; ++VertexIt)
		{
			Ar.Seek(VertexIt * SizePerVertex);

			TriangleValues.Reset();

			{
				RawAdjacency.MultiFind(VertexToUniqueIndexMap[VertexIt], TriangleValues);
			
				TriangleValues.Sort();
				TriangleValues.SetNum(Algo::Unique(TriangleValues));
			}

			MaxFoundAdjacentTriangleCount = FMath::Max(MaxFoundAdjacentTriangleCount, TriangleValues.Num());

			if (SortBySize)
			{
				const int32 AdjacentCount = TriangleValues.Num();

				TriangleSizes.Reset(AdjacentCount);
				SortIndices.Reset(AdjacentCount);
				SortedValues.Reset(AdjacentCount);
				
				for (int32 TriangleIt = 0; TriangleIt < AdjacentCount; ++TriangleIt)
				{
					const int32 TriangleIndex = TriangleValues[TriangleIt];

					const FVector& v0 = VertexBuffer->VertexPosition(IndexBuffer->Get(TriangleIndex * 3 + 0));
					const FVector& v1 = VertexBuffer->VertexPosition(IndexBuffer->Get(TriangleIndex * 3 + 1));
					const FVector& v2 = VertexBuffer->VertexPosition(IndexBuffer->Get(TriangleIndex * 3 + 2));

					const float TriangleSize = 0.5f * ((v2 - v0) ^ (v1 - v0)).Size();
					TriangleSizes.Add(TriangleSize);
					SortIndices.Add(TriangleIt);
				}

				Algo::StableSort(SortIndices, [&](int32 Lhs, int32 Rhs){ return TriangleSizes[Lhs] > TriangleSizes[Rhs]; });

				for (int32 TriangleIt = 0; TriangleIt < FMath::Min(AdjacentCount, MaxAdjacencyCount); ++TriangleIt)
				{
					SortedValues.Add(TriangleValues[SortIndices[TriangleIt]]);
				}

				Swap(SortedValues, TriangleValues);
			}

			TriangleIndexType AdjacentTriangleCount = FMath::Clamp<TriangleIndexType>(TriangleValues.Num(), 0, MaxAdjacencyCount);

			Ar.Serialize(TriangleValues.GetData(), AdjacentTriangleCount * sizeof(TriangleIndexType));
		}
	}

	return true;
}

void
FSkeletalMeshConnectivityProxy::Initialize(const FSkeletalMeshConnectivity& Connectivity)
{
	if (const FSkeletalMeshLODRenderData* LodRenderData = Connectivity.GetLodRenderData())
	{
		int32 MaxFoundAdjacentTriangleCount = 0;
		bool AdjacencySuccess = false;

		const auto IndexFormat = GetDefault<UNiagaraSettings>()->NDISkelMesh_AdjacencyTriangleIndexFormat;
		if (IndexFormat == ENDISkelMesh_AdjacencyTriangleIndexFormat::Full)
		{
			AdjacencySuccess = BuildAdjacencyBuffer<uint32, true>(*LodRenderData, MaxAdjacentTriangleCount, AdjacencyResource, MaxFoundAdjacentTriangleCount);
		}
		else if (IndexFormat == ENDISkelMesh_AdjacencyTriangleIndexFormat::Half)
		{
			AdjacencySuccess = BuildAdjacencyBuffer<uint16, true>(*LodRenderData, MaxAdjacentTriangleCount, AdjacencyResource, MaxFoundAdjacentTriangleCount);
		}

		if (!AdjacencySuccess)
		{
			UE_LOG(LogNiagara, Warning, TEXT("Failed to build adjacency for %s.  Check project settings for NDISkelMesh_AdjacencyTriangleIndexFormat.  Currently using %s."),
				*Connectivity.GetMeshName(), *StaticEnum<ENDISkelMesh_AdjacencyTriangleIndexFormat::Type>()->GetValueAsString(IndexFormat));
		}
		if (MaxFoundAdjacentTriangleCount > MaxAdjacentTriangleCount)
		{
			UE_LOG(LogNiagara, Warning, TEXT("Max adjacency limit of %d exceeded (up to %d found) when processing %s.  Some connections will be ignored."),
				MaxAdjacentTriangleCount, MaxFoundAdjacentTriangleCount, *Connectivity.GetMeshName());
		}
	}
}

void
FSkeletalMeshConnectivityProxy::InitRHI()
{
	FRHIResourceCreateInfo CreateInfo;
	CreateInfo.ResourceArray = &AdjacencyResource;

	const int32 BufferSize = AdjacencyResource.Num();

	AdjacencyBuffer = RHICreateVertexBuffer(BufferSize, BUF_ShaderResource | BUF_Static, CreateInfo);
	AdjacencySrv = RHICreateShaderResourceView(AdjacencyBuffer, sizeof(uint32), PF_R32_UINT);

#if STATS
	check(GpuMemoryUsage == 0);
	GpuMemoryUsage = BufferSize;
#endif

	INC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, GpuMemoryUsage);
}

void
FSkeletalMeshConnectivityProxy::ReleaseRHI()
{
	AdjacencyBuffer.SafeRelease();
	AdjacencySrv.SafeRelease();

	DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, GpuMemoryUsage);

#if STATS
	GpuMemoryUsage = 0;
#endif
}