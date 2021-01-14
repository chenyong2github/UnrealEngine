// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceSkeletalMeshUvMapping.h"

#include "NDISkeletalMeshCommon.h"
#include "NiagaraStats.h"

FSkeletalMeshUvMappingHandle::FSkeletalMeshUvMappingHandle()
	: UvMappingData(nullptr)
{
}

FSkeletalMeshUvMappingHandle::FSkeletalMeshUvMappingHandle(FSkeletalMeshUvMappingUsage InUsage, const TSharedPtr<FSkeletalMeshUvMapping>& InMappingData, bool bNeedsDataImmediately)
	: Usage(InUsage)
	, UvMappingData(InMappingData)
{
	if (FSkeletalMeshUvMapping* MappingData = UvMappingData.Get())
	{
		MappingData->RegisterUser(Usage, bNeedsDataImmediately);
	}
}

FSkeletalMeshUvMappingHandle::~FSkeletalMeshUvMappingHandle()
{
	if (FSkeletalMeshUvMapping* MappingData = UvMappingData.Get())
	{
		MappingData->UnregisterUser(Usage);
	}
}

FSkeletalMeshUvMappingHandle::FSkeletalMeshUvMappingHandle(FSkeletalMeshUvMappingHandle&& Other)
{
	Usage = Other.Usage;
	UvMappingData = Other.UvMappingData;
	Other.UvMappingData = nullptr;
}

FSkeletalMeshUvMappingHandle& FSkeletalMeshUvMappingHandle::operator=(FSkeletalMeshUvMappingHandle&& Other)
{
	if (this != &Other)
	{
		Usage = Other.Usage;
		UvMappingData = Other.UvMappingData;
		Other.UvMappingData = nullptr;
	}
	return *this;
}

FSkeletalMeshUvMappingHandle::operator bool() const
{
	return UvMappingData.IsValid();
}

void FSkeletalMeshUvMappingHandle::FindOverlappingTriangles(const FVector2D& InUv, TArray<int32>& TriangleIndices, float Tolerance) const
{
	TriangleIndices.Empty();
	if (UvMappingData)
	{
		UvMappingData->FindOverlappingTriangles(InUv, TriangleIndices, Tolerance);
	}
}

int32 FSkeletalMeshUvMappingHandle::FindFirstTriangle(const FVector2D& InUv, FVector& BarycentricCoord, float Tolerance) const
{
	if (UvMappingData)
	{
		return UvMappingData->FindFirstTriangle(InUv, BarycentricCoord, Tolerance);
	}

	return INDEX_NONE;
}

const FSkeletalMeshUvMappingBufferProxy* FSkeletalMeshUvMappingHandle::GetQuadTreeProxy() const
{
	if (UvMappingData)
	{
		return UvMappingData->GetQuadTreeProxy();
	}

	return nullptr;
}

FSkeletalMeshUvMapping::FSkeletalMeshUvMapping(TWeakObjectPtr<USkeletalMesh> InMeshObject, int32 InLodIndex, int32 InUvSetIndex)
	: LodIndex(InLodIndex)
	, UvSetIndex(InUvSetIndex)
	, MeshObject(InMeshObject)
	, TriangleIndexQuadTree(8 /* Internal node capacity */, 8 /* Maximum tree depth */)
	, CpuQuadTreeUserCount(0)
	, GpuQuadTreeUserCount(0)
{
}

void FSkeletalMeshUvMapping::BuildQuadTree()
{
	if (const FSkeletalMeshLODRenderData* LodRenderData = GetLodRenderData())
	{
		FSkelMeshVertexAccessor<false> MeshVertexAccessor;
		const FRawStaticIndexBuffer16or32Interface* IndexBuffer = LodRenderData->MultiSizeIndexContainer.GetIndexBuffer();
		const int32 TriangleCount = IndexBuffer->Num() / 3;

		for (int32 TriangleIt = 0; TriangleIt < TriangleCount; ++TriangleIt)
		{
			const FVector2D UVs[] =
			{
				MeshVertexAccessor.GetVertexUV(LodRenderData, IndexBuffer->Get(TriangleIt * 3 + 0), UvSetIndex),
				MeshVertexAccessor.GetVertexUV(LodRenderData, IndexBuffer->Get(TriangleIt * 3 + 1), UvSetIndex),
				MeshVertexAccessor.GetVertexUV(LodRenderData, IndexBuffer->Get(TriangleIt * 3 + 2), UvSetIndex),
			};

			TriangleIndexQuadTree.Insert(TriangleIt, FBox2D(UVs, UE_ARRAY_COUNT(UVs)));
		}
	}
}


class FResourceArrayWriter : public FMemoryArchive
{
public:
	FResourceArrayWriter(TResourceArray<uint8>& InBytes, bool bIsPersistent = false, bool bSetOffset = false, const FName InArchiveName = NAME_None)
		: FMemoryArchive()
		, Bytes(InBytes)
		, ArchiveName(InArchiveName)
	{
		this->SetIsSaving(true);
		this->SetIsPersistent(bIsPersistent);
		if (bSetOffset)
		{
			Offset = InBytes.Num();
		}
	}

	virtual void Serialize(void* Data, int64 Num) override
	{
		const int64 NumBytesToAdd = Offset + Num - Bytes.Num();
		if (NumBytesToAdd > 0)
		{
			const int64 NewArrayCount = Bytes.Num() + NumBytesToAdd;
			if (NewArrayCount >= MAX_int32)
			{
				UE_LOG(LogSerialization, Fatal, TEXT("FMemoryWriter does not support data larger than 2GB. Archive name: %s."), *ArchiveName.ToString());
			}

			Bytes.AddUninitialized((int32)NumBytesToAdd);
		}

		check((Offset + Num) <= Bytes.Num());

		if (Num)
		{
			FMemory::Memcpy(&Bytes[(int32)Offset], Data, Num);
			Offset += Num;
		}
	}
	/**
	 * Returns the name of the Archive.  Useful for getting the name of the package a struct or object
	 * is in when a loading error occurs.
	 *
	 * This is overridden for the specific Archive Types
	 **/
	virtual FString GetArchiveName() const override { return TEXT("FMemoryWriter"); }

	int64 TotalSize() override
	{
		return Bytes.Num();
	}

protected:

	TResourceArray<uint8>& Bytes;

	/** Archive name, used to debugging, by default set to NAME_None. */
	const FName ArchiveName;
};

void FSkeletalMeshUvMapping::FreezeQuadTree(TResourceArray<uint8>& OutQuadTree) const
{
	FResourceArrayWriter Ar(OutQuadTree);
	TriangleIndexQuadTree.Freeze(Ar);
}

void FSkeletalMeshUvMapping::ReleaseQuadTree()
{
	TriangleIndexQuadTree.Empty();
}

void FSkeletalMeshUvMapping::BuildGpuQuadTree()
{
	FrozenQuadTreeProxy.Initialize(*this);
	BeginInitResource(&FrozenQuadTreeProxy);
}

void FSkeletalMeshUvMapping::ReleaseGpuQuadTree()
{
	BeginReleaseResource(&FrozenQuadTreeProxy);
}

bool FSkeletalMeshUvMapping::IsUsed() const
{
	return (CpuQuadTreeUserCount > 0) || (GpuQuadTreeUserCount > 0);
}

void FSkeletalMeshUvMapping::RegisterUser(FSkeletalMeshUvMappingUsage Usage, bool bNeedsDataImmediately)
{
	if (Usage.RequiresCpuAccess || Usage.RequiresGpuAccess)
	{
		if (CpuQuadTreeUserCount.IncrementExchange() == 0)
		{
			BuildQuadTree();
		}
	}

	if (Usage.RequiresGpuAccess)
	{
		if (GpuQuadTreeUserCount.IncrementExchange() == 0)
		{
			BuildGpuQuadTree();
		}
	}
}

void FSkeletalMeshUvMapping::UnregisterUser(FSkeletalMeshUvMappingUsage Usage)
{
	if (Usage.RequiresCpuAccess || Usage.RequiresGpuAccess)
	{
		if (CpuQuadTreeUserCount.DecrementExchange() == 1)
		{
			ReleaseQuadTree();
		}
	}

	if (Usage.RequiresGpuAccess)
	{
		if (GpuQuadTreeUserCount.DecrementExchange() == 1)
		{
			ReleaseGpuQuadTree();
		}
	}
}

bool FSkeletalMeshUvMapping::Matches(const TWeakObjectPtr<USkeletalMesh>& InMeshObject, int32 InLodIndex, int32 InUvSetIndex) const
{
	return LodIndex == InLodIndex && MeshObject == InMeshObject && UvSetIndex == InUvSetIndex;
}

static FVector BuildTriangleCoordinate(const FSkeletalMeshLODRenderData& LodRenderData, const FVector2D& InUv, int32 InTriangleIndex, int32 InUvSetIndex)
{
	const FRawStaticIndexBuffer16or32Interface* IndexBuffer = LodRenderData.MultiSizeIndexContainer.GetIndexBuffer();
	const FSkelMeshVertexAccessor<false> MeshVertexAccessor;

	const FVector VertexUvs[] =
	{
		FVector(MeshVertexAccessor.GetVertexUV(&LodRenderData, IndexBuffer->Get(InTriangleIndex * 3 + 0), InUvSetIndex), 0.0f),
		FVector(MeshVertexAccessor.GetVertexUV(&LodRenderData, IndexBuffer->Get(InTriangleIndex * 3 + 1), InUvSetIndex), 0.0f),
		FVector(MeshVertexAccessor.GetVertexUV(&LodRenderData, IndexBuffer->Get(InTriangleIndex * 3 + 2), InUvSetIndex), 0.0f),
	};

	return FMath::GetBaryCentric2D(FVector(InUv, 0.0f), VertexUvs[0], VertexUvs[1], VertexUvs[2]);
}

void FSkeletalMeshUvMapping::FindOverlappingTriangles(const FVector2D& InUv, TArray<int32>& TriangleIndices, float Tolerance) const
{
	TriangleIndices.Empty();

	if (const FSkeletalMeshLODRenderData* LodRenderData = GetLodRenderData())
	{
		TArray<int32, TInlineAllocator<32>> Elements;

		FBox2D UvBox(InUv, InUv);
		TriangleIndexQuadTree.GetElements(UvBox, Elements);

		for (int32 TriangleIndex : Elements)
		{
			// generate the barycentric coordinates using the UVs of the triangle, the result may not be in the (0,1) range
			const FVector BarycentricCoord = BuildTriangleCoordinate(*LodRenderData, InUv, TriangleIndex, UvSetIndex);

			if ((BarycentricCoord.GetMin() > -Tolerance) && (BarycentricCoord.GetMax() < (1.0f + Tolerance)))
			{
				TriangleIndices.Add(TriangleIndex);
			}
		}
	}
}

int32 FSkeletalMeshUvMapping::FindFirstTriangle(const FVector2D& InUv, FVector& BarycentricCoord, float Tolerance) const
{
	if (const FSkeletalMeshLODRenderData* LodRenderData = GetLodRenderData())
	{
		TArray<int32, TInlineAllocator<32>> Elements;

		FBox2D UvBox(InUv, InUv);
		TriangleIndexQuadTree.GetElements(UvBox, Elements);

		for (int32 TriangleIndex : Elements)
		{
			// generate the barycentric coordinates using the UVs of the triangle, the result may not be in the (0,1) range
			BarycentricCoord = BuildTriangleCoordinate(*LodRenderData, InUv, TriangleIndex, UvSetIndex);

			if ((BarycentricCoord.GetMin() > -Tolerance) && (BarycentricCoord.GetMax() < (1.0f + Tolerance)))
			{
				return TriangleIndex;
			}
		}
	}

	return INDEX_NONE;
}

const FSkeletalMeshLODRenderData* FSkeletalMeshUvMapping::GetLodRenderData() const
{
	if (FSkeletalMeshRenderData* RenderData = MeshObject->GetResourceForRendering())
	{
		if (RenderData->LODRenderData.IsValidIndex(LodIndex))
		{
			const FSkeletalMeshLODRenderData& LodRenderData = RenderData->LODRenderData[LodIndex];
			const int32 NumTexCoords = LodRenderData.GetNumTexCoords();

			if (NumTexCoords > UvSetIndex)
			{
				return &LodRenderData;
			}
		}
	}

	return nullptr;
}

const FSkeletalMeshUvMappingBufferProxy* FSkeletalMeshUvMapping::GetQuadTreeProxy() const
{
	return &FrozenQuadTreeProxy;
}

FSkeletalMeshUvMappingBufferProxy::FSkeletalMeshUvMappingBufferProxy()
{

}

FSkeletalMeshUvMappingBufferProxy::~FSkeletalMeshUvMappingBufferProxy()
{

}

void
FSkeletalMeshUvMappingBufferProxy::Initialize(const FSkeletalMeshUvMapping& UvMapping)
{
	UvMapping.FreezeQuadTree(FrozenQuadTree);
}

void
FSkeletalMeshUvMappingBufferProxy::InitRHI()
{
	FRHIResourceCreateInfo CreateInfo;
	CreateInfo.ResourceArray = &FrozenQuadTree;

	const int32 BufferSize = FrozenQuadTree.Num();

	UvMappingBuffer = RHICreateVertexBuffer(BufferSize, BUF_ShaderResource | BUF_Static, CreateInfo);
	UvMappingSrv = RHICreateShaderResourceView(UvMappingBuffer, sizeof(int32), PF_R32_SINT);

#if STATS
	check(GpuMemoryUsage == 0);
	GpuMemoryUsage = BufferSize;
#endif

	INC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, GpuMemoryUsage);
}

void
FSkeletalMeshUvMappingBufferProxy::ReleaseRHI()
{
	UvMappingBuffer.SafeRelease();
	UvMappingSrv.SafeRelease();

	DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, GpuMemoryUsage);

#if STATS
	GpuMemoryUsage = 0;
#endif
}

