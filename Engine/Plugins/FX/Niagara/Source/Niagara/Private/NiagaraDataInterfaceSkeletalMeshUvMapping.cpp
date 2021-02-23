// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceSkeletalMeshUvMapping.h"

#include "NDISkeletalMeshCommon.h"
#include "NiagaraResourceArrayWriter.h"
#include "NiagaraStats.h"


template<bool UseFullPrecisionUv>
struct FQuadTreeQueryHelper
{
	const FNiagaraUvQuadTree& QuadTree;
	const FSkeletalMeshLODRenderData* LodRenderData;
	const FRawStaticIndexBuffer16or32Interface& IndexBuffer;
	const FSkelMeshVertexAccessor<UseFullPrecisionUv> MeshVertexAccessor;

	const int32 UvSetIndex;

	FQuadTreeQueryHelper(const FNiagaraUvQuadTree& InQuadTree, const FSkeletalMeshLODRenderData* InLodRenderData, int32 InUvSetIndex)
	: QuadTree(InQuadTree)
	, LodRenderData(InLodRenderData)
	, IndexBuffer(*LodRenderData->MultiSizeIndexContainer.GetIndexBuffer())
	, UvSetIndex(InUvSetIndex)
	{
	}

	FVector BuildTriangleCoordinate(const FVector2D& InUv, int32 TriangleIndex) const
	{
		const FVector VertexUvs[] =
		{
			FVector(MeshVertexAccessor.GetVertexUV(LodRenderData, IndexBuffer.Get(TriangleIndex * 3 + 0), UvSetIndex), 0.0f),
			FVector(MeshVertexAccessor.GetVertexUV(LodRenderData, IndexBuffer.Get(TriangleIndex * 3 + 1), UvSetIndex), 0.0f),
			FVector(MeshVertexAccessor.GetVertexUV(LodRenderData, IndexBuffer.Get(TriangleIndex * 3 + 2), UvSetIndex), 0.0f),
		};

		return FMath::GetBaryCentric2D(FVector(InUv, 0.0f), VertexUvs[0], VertexUvs[1], VertexUvs[2]);
	}

	void FindOverlappingTriangle(const FVector2D& InUv, float Tolerance, TArray<int32>& TriangleIndices) const
	{
		FBox2D UvBox(InUv, InUv);

		TArray<int32, TInlineAllocator<32>> Elements;
		QuadTree.GetElements(UvBox, Elements);

		for (int32 TriangleIndex : Elements)
		{
			// generate the barycentric coordinates using the UVs of the triangle, the result may not be in the (0,1) range
			const FVector BarycentricCoord = BuildTriangleCoordinate(InUv, TriangleIndex);

			if ((BarycentricCoord.GetMin() > -Tolerance) && (BarycentricCoord.GetMax() < (1.0f + Tolerance)))
			{
				TriangleIndices.Add(TriangleIndex);
			}
		}
	}

	int32 FindFirstTriangle(const FVector2D& InUv, float Tolerance, FVector& BarycentricCoord) const
	{
		int32 FoundTriangleIndex = INDEX_NONE;

		QuadTree.VisitElements(FBox2D(InUv, InUv), [&](int32 TriangleIndex)
		{
			// generate the barycentric coordinates using the UVs of the triangle, the result may not be in the (0,1) range
			FVector ElementCoord = BuildTriangleCoordinate(InUv, TriangleIndex);

			if ((ElementCoord.GetMin() > -Tolerance) && (ElementCoord.GetMax() < (1.0f + Tolerance)))
			{
				FoundTriangleIndex = TriangleIndex;
				BarycentricCoord = ElementCoord;
				return false;
			}

			return false;
		});

		return FoundTriangleIndex;
	}

	static bool NormalizedAabbTriangleOverlap(const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		const FVector2D TriAabbMin = FVector2D(FMath::Min3(A.X, B.X, C.X), FMath::Min3(A.Y, B.Y, C.Y));
		const FVector2D TriAabbMax = FVector2D(FMath::Max3(A.X, B.X, C.X), FMath::Max3(A.Y, B.Y, C.Y));

		if (TriAabbMin.GetMax() > 1.0f || TriAabbMax.GetMin() < 0.0f)
		{
			return false;
		}

		const FVector2D TriangleEdges[] = { C - B, A - C, B - A };

		for (int32 i = 0; i < UE_ARRAY_COUNT(TriangleEdges); ++i)
		{
			const FVector2D SeparatingAxis(-TriangleEdges[i].Y, TriangleEdges[i].X);
			float AabbSegmentMin = FMath::Min(0.0f, FMath::Min3(SeparatingAxis.X, SeparatingAxis.Y, SeparatingAxis.X + SeparatingAxis.Y));
			float AabbSegmentMax = FMath::Max(0.0f, FMath::Max3(SeparatingAxis.X, SeparatingAxis.Y, SeparatingAxis.X + SeparatingAxis.Y));
			float TriangleSegmentMin = FMath::Min3(FVector2D::DotProduct(A, SeparatingAxis), FVector2D::DotProduct(B, SeparatingAxis), FVector2D::DotProduct(C, SeparatingAxis));
			float TriangleSegmentMax = FMath::Max3(FVector2D::DotProduct(A, SeparatingAxis), FVector2D::DotProduct(B, SeparatingAxis), FVector2D::DotProduct(C, SeparatingAxis));

			if (AabbSegmentMin > TriangleSegmentMax || AabbSegmentMax < TriangleSegmentMax)
			{
				return false;
			}
		}

		return true;
	}

	int32 FindFirstTriangle(const FBox2D& InUvBox, FVector& BarycentricCoord) const
	{
		int32 FoundTriangleIndex = INDEX_NONE;
		const FVector2D NormalizeScale = FVector2D(1.0f, 1.0f) / (InUvBox.Max - InUvBox.Min);
		const FVector2D NormalizeBias = FVector2D(1.0f, 1.0f) - InUvBox.Max * NormalizeScale;
		const FVector UvRef = FVector(InUvBox.GetCenter(), 0.0f);

		QuadTree.VisitElements(InUvBox, [&](int32 TriangleIndex)
		{
			const FVector2D A = MeshVertexAccessor.GetVertexUV(LodRenderData, IndexBuffer.Get(TriangleIndex * 3 + 0), UvSetIndex);
			const FVector2D B = MeshVertexAccessor.GetVertexUV(LodRenderData, IndexBuffer.Get(TriangleIndex * 3 + 1), UvSetIndex);
			const FVector2D C = MeshVertexAccessor.GetVertexUV(LodRenderData, IndexBuffer.Get(TriangleIndex * 3 + 2), UvSetIndex);

			// evaluate if the triangle overlaps with the InUvBox
			if (!NormalizedAabbTriangleOverlap(NormalizeScale * A + NormalizeBias, NormalizeScale * B + NormalizeBias, NormalizeScale * C + NormalizeBias))
			{
				return true;
			}

			BarycentricCoord = FMath::GetBaryCentric2D(UvRef, FVector(A, 0.0f), FVector(B, 0.0f), FVector(C, 0.0f));
			FoundTriangleIndex = TriangleIndex;
			return false;
		});

		return FoundTriangleIndex;
	}
};

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

bool FSkeletalMeshUvMapping::IsValidMeshObject(TWeakObjectPtr<USkeletalMesh>& MeshObject, int32 InLodIndex, int32 InUvSetIndex)
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

	if (!GetLodRenderData(*Mesh, InLodIndex, InUvSetIndex))
	{
		// no render data available
		return false;
	}

	return true;
}

void FSkeletalMeshUvMappingHandle::FindOverlappingTriangles(const FVector2D& InUv, float Tolerance, TArray<int32>& TriangleIndices) const
{
	TriangleIndices.Empty();
	if (UvMappingData)
	{
		UvMappingData->FindOverlappingTriangles(InUv, Tolerance, TriangleIndices);
	}
}

int32 FSkeletalMeshUvMappingHandle::FindFirstTriangle(const FVector2D& InUv, float Tolerance, FVector& BarycentricCoord) const
{
	if (UvMappingData)
	{
		return UvMappingData->FindFirstTriangle(InUv, Tolerance, BarycentricCoord);
	}

	return INDEX_NONE;
}

int32 FSkeletalMeshUvMappingHandle::FindFirstTriangle(const FBox2D& InUvBox, FVector& BarycentricCoord) const
{
	if (UvMappingData)
	{
		return UvMappingData->FindFirstTriangle(InUvBox, BarycentricCoord);
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

int32 FSkeletalMeshUvMappingHandle::GetUvSetIndex() const
{
	if (UvMappingData)
	{
		return UvMappingData->UvSetIndex;
	}

	return 0;
}

int32 FSkeletalMeshUvMappingHandle::GetLodIndex() const
{
	if (UvMappingData)
	{
		return UvMappingData->LodIndex;
	}

	return 0;
}


FSkeletalMeshUvMapping::FSkeletalMeshUvMapping(TWeakObjectPtr<USkeletalMesh> InMeshObject, int32 InLodIndex, int32 InUvSetIndex)
	: LodIndex(InLodIndex)
	, UvSetIndex(InUvSetIndex)
	, MeshObject(InMeshObject)
	, TriangleIndexQuadTree(8 /* Internal node capacity */, 8 /* Maximum tree depth */)
	, CpuQuadTreeUserCount(0)
	, GpuQuadTreeUserCount(0)
	, ReleasedByRT(false)
	, QueuedForRelease(false)
{
}

template<bool UseFullPrecisionUv>
static void BuildQuadTreeHelper(FNiagaraUvQuadTree& QuadTree, const FSkeletalMeshLODRenderData* LodRenderData, int32 UvSetIndex)
{
	FSkelMeshVertexAccessor<UseFullPrecisionUv> MeshVertexAccessor;
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

		// we want to skip degenerate triangles
		if (FMath::Abs((UVs[1] - UVs[0]) ^ (UVs[2] - UVs[0])) < SMALL_NUMBER)
		{
			continue;
		}

		QuadTree.Insert(TriangleIt, FBox2D(UVs, UE_ARRAY_COUNT(UVs)));
	}
}

void FSkeletalMeshUvMapping::BuildQuadTree()
{
	if (const FSkeletalMeshLODRenderData* LodRenderData = GetLodRenderData())
	{
		if (LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs())
		{
			BuildQuadTreeHelper<true>(TriangleIndexQuadTree, LodRenderData, UvSetIndex);
		}
		else
		{
			BuildQuadTreeHelper<false>(TriangleIndexQuadTree, LodRenderData, UvSetIndex);
		}
	}
}

void FSkeletalMeshUvMapping::FreezeQuadTree(TResourceArray<uint8>& OutQuadTree) const
{
	FNiagaraResourceArrayWriter Ar(OutQuadTree);
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
	QueuedForRelease = true;
	ReleasedByRT = false;
	FThreadSafeBool* Released = &ReleasedByRT;

	BeginReleaseResource(&FrozenQuadTreeProxy);

	ENQUEUE_RENDER_COMMAND(BeginDestroyCommand)(
		[Released](FRHICommandListImmediate& RHICmdList)
		{
			*Released = true;
		});
}

bool FSkeletalMeshUvMapping::IsUsed() const
{
	return (CpuQuadTreeUserCount > 0)
		|| (GpuQuadTreeUserCount > 0);
}

bool FSkeletalMeshUvMapping::CanBeDestroyed() const
{
	return !IsUsed() && (!QueuedForRelease || ReleasedByRT);
}

void FSkeletalMeshUvMapping::RegisterUser(FSkeletalMeshUvMappingUsage Usage, bool bNeedsDataImmediately)
{
	if (Usage.RequiresCpuAccess || Usage.RequiresGpuAccess)
	{
		if (CpuQuadTreeUserCount++ == 0)
		{
			BuildQuadTree();
		}
	}

	if (Usage.RequiresGpuAccess)
	{
		if (GpuQuadTreeUserCount++ == 0)
		{
			BuildGpuQuadTree();
		}
	}
}

void FSkeletalMeshUvMapping::UnregisterUser(FSkeletalMeshUvMappingUsage Usage)
{
	if (Usage.RequiresCpuAccess || Usage.RequiresGpuAccess)
	{
		if (--CpuQuadTreeUserCount == 0)
		{
			ReleaseQuadTree();
		}
	}

	if (Usage.RequiresGpuAccess)
	{
		if (--GpuQuadTreeUserCount == 0)
		{
			ReleaseGpuQuadTree();
		}
	}
}

bool FSkeletalMeshUvMapping::Matches(const TWeakObjectPtr<USkeletalMesh>& InMeshObject, int32 InLodIndex, int32 InUvSetIndex) const
{
	return LodIndex == InLodIndex && MeshObject == InMeshObject && UvSetIndex == InUvSetIndex;
}

void FSkeletalMeshUvMapping::FindOverlappingTriangles(const FVector2D& InUv, float Tolerance, TArray<int32>& TriangleIndices) const
{
	TriangleIndices.Empty();

	if (const FSkeletalMeshLODRenderData* LodRenderData = GetLodRenderData())
	{
		if (LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs())
		{
			FQuadTreeQueryHelper<true> QueryHelper(TriangleIndexQuadTree, LodRenderData, UvSetIndex);
			QueryHelper.FindOverlappingTriangle(InUv, Tolerance, TriangleIndices);
		}
		else
		{
			FQuadTreeQueryHelper<false> QueryHelper(TriangleIndexQuadTree, LodRenderData, UvSetIndex);
			QueryHelper.FindOverlappingTriangle(InUv, Tolerance, TriangleIndices);
		}
	}
}

int32 FSkeletalMeshUvMapping::FindFirstTriangle(const FVector2D& InUv, float Tolerance, FVector& BarycentricCoord) const
{
	if (const FSkeletalMeshLODRenderData* LodRenderData = GetLodRenderData())
	{
		if (LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs())
		{
			FQuadTreeQueryHelper<true> QueryHelper(TriangleIndexQuadTree, LodRenderData, UvSetIndex);
			return QueryHelper.FindFirstTriangle(InUv, Tolerance, BarycentricCoord);
		}
		else
		{
			FQuadTreeQueryHelper<false> QueryHelper(TriangleIndexQuadTree, LodRenderData, UvSetIndex);
			return QueryHelper.FindFirstTriangle(InUv, Tolerance, BarycentricCoord);
		}
	}

	return INDEX_NONE;
}

int32 FSkeletalMeshUvMapping::FindFirstTriangle(const FBox2D& InUvBox, FVector& BarycentricCoord) const
{
	if (const FSkeletalMeshLODRenderData* LodRenderData = GetLodRenderData())
	{
		if (LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs())
		{
			FQuadTreeQueryHelper<true> QueryHelper(TriangleIndexQuadTree, LodRenderData, UvSetIndex);
			return QueryHelper.FindFirstTriangle(InUvBox, BarycentricCoord);
		}
		else
		{
			FQuadTreeQueryHelper<false> QueryHelper(TriangleIndexQuadTree, LodRenderData, UvSetIndex);
			return QueryHelper.FindFirstTriangle(InUvBox, BarycentricCoord);
		}
	}

	return INDEX_NONE;
}

const FSkeletalMeshLODRenderData* FSkeletalMeshUvMapping::GetLodRenderData(const USkeletalMesh& Mesh, int32 LodIndex, int32 UvSetIndex)
{
	if (const FSkeletalMeshRenderData* RenderData = Mesh.GetResourceForRendering())
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

const FSkeletalMeshLODRenderData* FSkeletalMeshUvMapping::GetLodRenderData() const
{
	if (const USkeletalMesh* Mesh = MeshObject.Get())
	{
		return GetLodRenderData(*Mesh, LodIndex, UvSetIndex);
	}
	return nullptr;
}

const FSkeletalMeshUvMappingBufferProxy* FSkeletalMeshUvMapping::GetQuadTreeProxy() const
{
	return &FrozenQuadTreeProxy;
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

