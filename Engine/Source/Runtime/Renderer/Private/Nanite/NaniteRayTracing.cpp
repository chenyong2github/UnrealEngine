// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteRayTracing.h"

#if RHI_RAYTRACING

#include "Rendering/NaniteStreamingManager.h"

#include "NaniteStreamOut.h"
#include "NaniteSceneProxy.h"
#include "NaniteShared.h"

#include "ShaderPrintParameters.h"

#include "PrimitiveSceneInfo.h"
#include "ScenePrivate.h"
#include "SceneInterface.h"

#include "RenderGraphUtils.h"

static bool GNaniteRayTracingUpdate = true;
static FAutoConsoleVariableRef CVarNaniteRayTracingUpdate(
	TEXT("r.RayTracing.Nanite.Update"),
	GNaniteRayTracingUpdate,
	TEXT("Whether to process Nanite RayTracing update requests."),
	ECVF_RenderThreadSafe
);

static bool GNaniteRayTracingForceUpdateVisible = false;
static FAutoConsoleVariableRef CVarNaniteRayTracingForceUpdateVisible(
	TEXT("r.RayTracing.Nanite.ForceUpdateVisible"),
	GNaniteRayTracingForceUpdateVisible,
	TEXT("Force BLAS of visible primitives to be updated next frame."),
	ECVF_RenderThreadSafe
);

static float GNaniteRayTracingCutError = 0.0f;
static FAutoConsoleVariableRef CVarNaniteRayTracingCutError(
	TEXT("r.RayTracing.Nanite.CutError"),
	GNaniteRayTracingCutError,
	TEXT("Global target cut error to control quality when using procedural raytracing geometry for Nanite meshes."),
	ECVF_RenderThreadSafe
);

static int32 GNaniteRayTracingMaxNumVertices = 16 * 1024 * 1024;
static FAutoConsoleVariableRef CVarNaniteRayTracingMaxNumVertices(
	TEXT("r.RayTracing.Nanite.StreamOut.MaxNumVertices"),
	GNaniteRayTracingMaxNumVertices,
	TEXT("Max number of vertices to stream out per frame."),
	ECVF_ReadOnly
);

static int32 GNaniteRayTracingMaxNumIndices = 64 * 1024 * 1024;
static FAutoConsoleVariableRef CVarNaniteRayTracingMaxNumIndices(
	TEXT("r.RayTracing.Nanite.StreamOut.MaxNumIndices"),
	GNaniteRayTracingMaxNumIndices,
	TEXT("Max number of indices to stream out per frame."),
	ECVF_ReadOnly
);

DECLARE_GPU_STAT(RebuildNaniteBLAS);

namespace Nanite
{
	static FRDGBufferRef ResizeBufferIfNeeded(FRDGBuilder& GraphBuilder, TRefCountPtr<FRDGPooledBuffer>& ExternalBuffer, uint32 BytesPerElement, uint32 NumElements, const TCHAR* Name)
	{
		FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateStructuredDesc(BytesPerElement, NumElements);

		FRDGBufferRef BufferRDG = GraphBuilder.RegisterExternalBuffer(ExternalBuffer);

		if (BufferDesc.GetSize() > BufferRDG->GetSize())
		{
			BufferRDG = GraphBuilder.CreateBuffer(BufferDesc, Name);
		}

		return BufferRDG;
	}

	FRayTracingManager::FRayTracingManager()
	{

	}

	FRayTracingManager::~FRayTracingManager()
	{

	}

	void FRayTracingManager::InitRHI()
	{
		if (!DoesPlatformSupportNanite(GMaxRHIShaderPlatform))
		{
			return;
		}

		AuxiliaryDataBuffer = AllocatePooledBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 8), TEXT("NaniteRayTracing.AuxiliaryDataBuffer"));

		ReadbackBuffers.AddZeroed(MaxReadbackBuffers);

		for (auto& ReadbackData : ReadbackBuffers)
		{
			ReadbackData.MeshDataReadbackBuffer = new FRHIGPUBufferReadback(TEXT("NaniteRayTracing.MeshDataReadbackBuffer"));
		}

		FNaniteRayTracingUniformParameters Params = {};
		// Use AuxiliaryDataBuffer as placeholder when creating the uniform buffer
		// This is later updated with the correct SRVs
		Params.ClusterPageData = AuxiliaryDataBuffer->GetSRV();
		Params.HierarchyBuffer = AuxiliaryDataBuffer->GetSRV();
		Params.RayTracingDataBuffer = AuxiliaryDataBuffer->GetSRV();

		UniformBuffer = TUniformBufferRef<FNaniteRayTracingUniformParameters>::CreateUniformBufferImmediate(Params, UniformBuffer_MultiFrame);
	}

	void FRayTracingManager::ReleaseRHI()
	{
		if (!DoesPlatformSupportNanite(GMaxRHIShaderPlatform))
		{
			return;
		}
	}

	void FRayTracingManager::Add(FPrimitiveSceneInfo* SceneInfo)
	{
		if (!IsRayTracingEnabled())
		{
			return;
		}

		auto NaniteProxy = static_cast<Nanite::FSceneProxyBase*>(SceneInfo->Proxy);

		uint32 NaniteResourceID = INDEX_NONE;
		uint32 NaniteHierarchyOffset = INDEX_NONE;
		uint32 NaniteImposterIndex = INDEX_NONE;
		NaniteProxy->GetNaniteResourceInfo(NaniteResourceID, NaniteHierarchyOffset, NaniteImposterIndex);

		// TODO: Should use both ResourceID and HierarchyOffset as identifier for raytracing geometry
		// For example, FNaniteGeometryCollectionSceneProxy can use the same ResourceID with different HierarchyOffsets
		// (FNaniteGeometryCollectionSceneProxy are not supported in raytracing yet)
		uint32& Id = ResourceToRayTracingIdMap.FindOrAdd(NaniteResourceID, INDEX_NONE);

		FInternalData* Data;

		if (Id == INDEX_NONE)
		{
			Nanite::FResourceMeshInfo MeshInfo = NaniteProxy->GetResourceMeshInfo();
			check(MeshInfo.NumClusters);

			Data = new FInternalData;

			Data->ResourceId = NaniteResourceID;
			Data->HierarchyOffset = NaniteHierarchyOffset;
			Data->NumClusters = MeshInfo.NumClusters;
			Data->NumNodes = MeshInfo.NumNodes;
			Data->NumVertices = MeshInfo.NumVertices;
			Data->NumTriangles = MeshInfo.NumTriangles;
			Data->NumMaterials = MeshInfo.NumMaterials;
			Data->NumSegments = MeshInfo.NumSegments;
			Data->SegmentMapping = MeshInfo.SegmentMapping;
			Data->DebugName = MeshInfo.DebugName;

			Data->PrimitiveId = INDEX_NONE;

			// TODO: Try allocating auxiliary range on GPU (would require updating GPUScene entry to have the correct offset after rebuild
			Data->AuxiliaryDataOffset = AuxiliaryDataAllocator.Allocate(MeshInfo.NumClusters * NANITE_MAX_CLUSTER_TRIANGLES);

			Id = Geometries.Add(Data);

			UpdateRequests.Add(Id);
		}
		else
		{
			Data = Geometries[Id];
		}

		Data->Primitives.Add(SceneInfo);

		if (Data->RayTracingGeometryRHI)
		{
			// Patch CachedRayTracingInstance here since CacheRayTracingPrimitives(...) is called before Primitive is added to Nanite::FRayTracingManager
			SceneInfo->CachedRayTracingInstance.GeometryRHI = Data->RayTracingGeometryRHI;
		}

		PendingRemoves.Remove(Id);

		NaniteProxy->SetRayTracingId(Id);
		NaniteProxy->SetRayTracingDataOffset(Data->AuxiliaryDataOffset);
	}

	void FRayTracingManager::Remove(FPrimitiveSceneInfo* SceneInfo)
	{
		if (!IsRayTracingEnabled())
		{
			return;
		}

		auto NaniteProxy = static_cast<Nanite::FSceneProxyBase*>(SceneInfo->Proxy);

		const uint32 Id = NaniteProxy->GetRayTracingId();
		check(Id != INDEX_NONE);

		FInternalData* Data = Geometries[Id];

		Data->Primitives.Remove(SceneInfo);
		if (Data->Primitives.IsEmpty())
		{
			PendingRemoves.Add(Id);
		}

		NaniteProxy->SetRayTracingId(INDEX_NONE);
		NaniteProxy->SetRayTracingDataOffset(INDEX_NONE);
	}

	void FRayTracingManager::RequestUpdates(const TSet<uint32>& InUpdateRequests)
	{
		if (!IsRayTracingEnabled())
		{
			return;
		}

		for (uint32 ResourceId : InUpdateRequests)
		{
			uint32* Id = ResourceToRayTracingIdMap.Find(ResourceId);

			if (Id != nullptr)
			{
				UpdateRequests.Add(*Id);
			}
		}
	}

	void FRayTracingManager::AddVisiblePrimitive(const FPrimitiveSceneInfo* SceneInfo)
	{
		check(GetRayTracingMode() != ERayTracingMode::Fallback);

		auto NaniteProxy = static_cast<Nanite::FSceneProxyBase*>(SceneInfo->Proxy);

		const uint32 Id = NaniteProxy->GetRayTracingId();
		check(Id != INDEX_NONE);

		FInternalData* Data = Geometries[Id];
		Data->PrimitiveId = SceneInfo->GetIndex(); // TODO: Update this when index changes?

		VisibleGeometries.Add(Id);
	}

	FRDGBufferRef FRayTracingManager::ResizeAuxiliaryDataBufferIfNeeded(FRDGBuilder& GraphBuilder)
	{
		const uint32 NumAuxiliaryDataEntries = FMath::Max(AuxiliaryDataAllocator.GetMaxSize(), 32);

		FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumAuxiliaryDataEntries);

		FRDGBufferRef BufferRDG = GraphBuilder.RegisterExternalBuffer(AuxiliaryDataBuffer);

		if (BufferDesc.GetSize() > BufferRDG->GetSize())
		{
			FRDGBufferRef SrcBufferRDG = BufferRDG;

			BufferRDG = GraphBuilder.CreateBuffer(BufferDesc, TEXT("NaniteRayTracing.AuxiliaryDataBuffer"));

			AddCopyBufferPass(GraphBuilder, BufferRDG, SrcBufferRDG);
		}

		//ResizeStructuredBufferIfNeeded(GraphBuilder, AuxiliaryDataBuffer, NumAuxiliaryDataEntries * sizeof(uint32), TEXT("NaniteRayTracing.AuxiliaryDataBuffer"));

		return BufferRDG;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FNaniteRayTracingPrimitivesParams, )
		RDG_BUFFER_ACCESS(Buffer0, ERHIAccess::SRVCompute)
		RDG_BUFFER_ACCESS(Buffer1, ERHIAccess::SRVCompute)
		RDG_BUFFER_ACCESS(ScratchBuffer, ERHIAccess::UAVCompute)
	END_SHADER_PARAMETER_STRUCT()

	void FRayTracingManager::ProcessUpdateRequests(FRDGBuilder& GraphBuilder, FShaderResourceViewRHIRef GPUScenePrimitiveBufferSRV)
	{
		if (GNaniteRayTracingForceUpdateVisible)
		{
			UpdateRequests.Append(VisibleGeometries);
			GNaniteRayTracingForceUpdateVisible = false;
		}

		if (!GNaniteRayTracingUpdate || GetRayTracingMode() == ERayTracingMode::Fallback || bUpdating || UpdateRequests.IsEmpty())
		{
			VisibleGeometries.Empty();
			return;
		}

		TSet<uint32> ToUpdate;

		for (uint32 GeometryId : VisibleGeometries)
		{
			if (UpdateRequests.Contains(GeometryId))
			{
				UpdateRequests.Remove(GeometryId);
				ToUpdate.Add(GeometryId);
			}
		}

		VisibleGeometries.Empty();

		if (ToUpdate.IsEmpty())
		{
			return;
		}

		bUpdating = true;

		FReadbackData& ReadbackData = ReadbackBuffers[ReadbackBuffersWriteIndex];

		// Upload geometry data
		FRDGBufferRef RequestBuffer = nullptr;
		FRDGBufferRef SegmentMappingBuffer = nullptr;

		uint32 MeshDataSize = 0;
		
		{
			uint32 NumsSegmentMappingEntries = 0;
			for (auto GeometryId : ToUpdate)
			{
				FInternalData& Data = *Geometries[GeometryId];
				NumsSegmentMappingEntries += Data.SegmentMapping.Num();
			}

			FRDGUploadData<FStreamOutRequest> UploadData(GraphBuilder, ToUpdate.Num());
			FRDGUploadData<uint32> SegmentMappingUploadData(GraphBuilder, NumsSegmentMappingEntries);

			uint32 Index = 0;
			uint32 SegmentMappingOffset = 0;

			for (auto GeometryId : ToUpdate)
			{
				FInternalData& Data = *Geometries[GeometryId];

				check(!Data.bUpdating);
				Data.bUpdating = true;

				check(Data.BaseMeshDataOffset == -1);
				Data.BaseMeshDataOffset = MeshDataSize;

				FStreamOutRequest& Request = UploadData[Index];
				Request.PrimitiveId = Data.PrimitiveId;
				Request.NumMaterials = Data.NumMaterials;
				Request.NumSegments = Data.NumSegments;
				Request.SegmentMappingOffset = SegmentMappingOffset;
				Request.AuxiliaryDataOffset = Data.AuxiliaryDataOffset;
				Request.MeshDataOffset = Data.BaseMeshDataOffset;

				for (uint32 SegmentIndex : Data.SegmentMapping)
				{
					SegmentMappingUploadData[SegmentMappingOffset] = SegmentIndex;
					++SegmentMappingOffset;
				}

				MeshDataSize += (3 + 2 * Data.NumSegments); // one entry per mesh

				ReadbackData.Entries.Add(GeometryId);

				++Index;
			}

			RequestBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("NaniteRayTracing.RequestBuffer"), UploadData);

			SegmentMappingBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("NaniteRayTracing.SegmentMappingBuffer"), SegmentMappingUploadData);
		}

		FRDGBufferRef MeshDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), FMath::Max(MeshDataSize, 32U)), TEXT("NaniteStreamOut.MeshDataBuffer"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MeshDataBuffer), 0);

		FRDGBufferRef AuxiliaryDataBufferRDG = ResizeAuxiliaryDataBufferIfNeeded(GraphBuilder);
		AuxiliaryDataBuffer = GraphBuilder.ConvertToExternalBuffer(AuxiliaryDataBufferRDG);

		if (VertexBuffer == nullptr)
		{
			VertexBuffer = AllocatePooledBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), GNaniteRayTracingMaxNumVertices * 3), TEXT("NaniteRayTracing.VertexBuffer"));
		}

		if (IndexBuffer == nullptr)
		{
			IndexBuffer = AllocatePooledBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), GNaniteRayTracingMaxNumIndices), TEXT("NaniteRayTracing.IndexBuffer"));
		}

		FRDGBufferRef VertexBufferRDG = GraphBuilder.RegisterExternalBuffer(VertexBuffer);
		FRDGBufferRef IndexBufferRDG = GraphBuilder.RegisterExternalBuffer(IndexBuffer);

		StreamOutData(
			GraphBuilder,
			GetGlobalShaderMap(GetFeatureLevel()),
			GPUScenePrimitiveBufferSRV,
			NodesAndClusterBatchesBuffer,
			GetCutError(),
			ToUpdate.Num(),
			RequestBuffer,
			SegmentMappingBuffer,
			MeshDataBuffer,
			AuxiliaryDataBufferRDG,
			VertexBufferRDG,
			GNaniteRayTracingMaxNumVertices,
			IndexBufferRDG,
			GNaniteRayTracingMaxNumIndices);

		// readback
		{
			AddReadbackBufferPass(GraphBuilder, RDG_EVENT_NAME("NaniteRayTracing::Readback"), MeshDataBuffer,
				[MeshDataReadbackBuffer = ReadbackData.MeshDataReadbackBuffer, MeshDataBuffer](FRHICommandList& RHICmdList)
			{
				MeshDataReadbackBuffer->EnqueueCopy(RHICmdList, MeshDataBuffer->GetRHI(), 0u);
			});

			ReadbackData.MeshDataSize = MeshDataSize;

			ReadbackBuffersWriteIndex = (ReadbackBuffersWriteIndex + 1u) % MaxReadbackBuffers;
			ReadbackBuffersNumPending = FMath::Min(ReadbackBuffersNumPending + 1u, MaxReadbackBuffers);
		}

		ToUpdate.Empty();
	}

	void FRayTracingManager::Update()
	{
		// process PendingRemoves
		{
			TSet<uint32> StillPendingRemoves;

			for (uint32 GeometryId : PendingRemoves)
			{
				FInternalData* Data = Geometries[GeometryId];

				if (Data->bUpdating)
				{
					// can't remove until update is finished, delay to next frame
					StillPendingRemoves.Add(GeometryId);
				}
				else
				{
					AuxiliaryDataAllocator.Free(Data->AuxiliaryDataOffset, Data->NumClusters * NANITE_MAX_CLUSTER_TRIANGLES);
					ResourceToRayTracingIdMap.Remove(Data->ResourceId);
					Geometries.RemoveAt(GeometryId);
					delete (Data);
				}
			}

			Swap(PendingRemoves, StillPendingRemoves);
		}

		while (ReadbackBuffersNumPending > 0)
		{
			uint32 Index = (ReadbackBuffersWriteIndex + MaxReadbackBuffers - ReadbackBuffersNumPending) % MaxReadbackBuffers;
			FReadbackData& ReadbackData = ReadbackBuffers[Index];
			if (ReadbackData.MeshDataReadbackBuffer->IsReady())
			{
				ReadbackBuffersNumPending--;

				const uint32* MeshDataReadbackBufferPtr = (const uint32*)ReadbackData.MeshDataReadbackBuffer->Lock(ReadbackData.MeshDataSize * sizeof(uint32));

				for (int32 GeometryIndex = 0; GeometryIndex < ReadbackData.Entries.Num(); ++GeometryIndex)
				{
					uint32 GeometryId = ReadbackData.Entries[GeometryIndex];
					FInternalData& Data = *Geometries[GeometryId];

					const uint32 VertexBufferOffset = MeshDataReadbackBufferPtr[Data.BaseMeshDataOffset + 0];
					const uint32 IndexBufferOffset = MeshDataReadbackBufferPtr[Data.BaseMeshDataOffset + 1];

					if (VertexBufferOffset == 0xFFFFFFFFu || IndexBufferOffset == 0xFFFFFFFFu)
					{
						// ran out of space in StreamOut buffers
						Data.bUpdating = false;
						Data.BaseMeshDataOffset = -1;
						UpdateRequests.Add(GeometryId); // request update again
						continue;

						// TODO:
						// - Resize VB/IB buffers dynamically instead of always allocating max size
						// - Warn user if max VB/IB buffer size are not large enough for a specific mesh cut
						// - Store vertices and indices in the same buffer in a single allocation
					}

					const uint32 NumVertices = MeshDataReadbackBufferPtr[Data.BaseMeshDataOffset + 2];

					FRayTracingGeometryInitializer Initializer;
					Initializer.DebugName = Data.DebugName;
// 					Initializer.bFastBuild = false;
// 					Initializer.bAllowUpdate = false;
					Initializer.bAllowCompaction = false;

					Initializer.IndexBuffer = IndexBuffer->GetRHI();
					Initializer.IndexBufferOffset = IndexBufferOffset * sizeof(uint32);

					Initializer.TotalPrimitiveCount = 0;

					Initializer.Segments.SetNum(Data.NumSegments);

					for (uint32 SegmentIndex = 0; SegmentIndex < Data.NumSegments; ++SegmentIndex)
					{
						const uint32 NumIndices = MeshDataReadbackBufferPtr[Data.BaseMeshDataOffset + 3 + (SegmentIndex * 2)];
						const uint32 FirstIndex = MeshDataReadbackBufferPtr[Data.BaseMeshDataOffset + 4 + (SegmentIndex * 2)];

						FRayTracingGeometrySegment& Segment = Initializer.Segments[SegmentIndex];
						Segment.FirstPrimitive = FirstIndex / 3;
						Segment.NumPrimitives = NumIndices / 3;
						Segment.VertexBuffer = VertexBuffer->GetRHI();
						Segment.VertexBufferOffset = VertexBufferOffset * sizeof(FVector3f);
						Segment.MaxVertices = NumVertices;

						Initializer.TotalPrimitiveCount += Segment.NumPrimitives;
					}

					Data.RayTracingGeometryRHI = RHICreateRayTracingGeometry(Initializer);

					for (auto& Primitive : Data.Primitives)
					{
						Primitive->CachedRayTracingInstance.GeometryRHI = Data.RayTracingGeometryRHI;
					}

					PendingBuilds.Add(GeometryId);
				}

				ReadbackData.Entries.Empty();
				ReadbackData.MeshDataReadbackBuffer->Unlock();
			}
			else
			{
				break;
			}
		}
	}

	bool FRayTracingManager::ProcessBuildRequests(FRDGBuilder& GraphBuilder)
	{
		TArray<FRayTracingGeometryBuildParams> BuildParams;
		uint32 BLASScratchSize = 0;

		for (uint32 GeometryId : PendingBuilds)
		{
			FInternalData& Data = *Geometries[GeometryId];

			const FRayTracingGeometryInitializer& Initializer = Data.RayTracingGeometryRHI->GetInitializer();

			FRayTracingGeometryBuildParams Params;
			Params.Geometry = Data.RayTracingGeometryRHI;
			Params.BuildMode = EAccelerationStructureBuildMode::Build;

			BuildParams.Add(Params);

			FRayTracingAccelerationStructureSize SizeInfo = RHICalcRayTracingGeometrySize(Initializer);
			BLASScratchSize = Align(BLASScratchSize + SizeInfo.BuildScratchSize, GRHIRayTracingScratchBufferAlignment);

			Data.BaseMeshDataOffset = -1;
			Data.bUpdating = false;
		}

		PendingBuilds.Empty();

		bool bAnyBlasRebuilt = false;

		if (BuildParams.Num() > 0)
		{
			RDG_GPU_STAT_SCOPE(GraphBuilder, RebuildNaniteBLAS);

			FRDGBufferDesc ScratchBufferDesc;
			ScratchBufferDesc.Usage = EBufferUsageFlags::RayTracingScratch | EBufferUsageFlags::StructuredBuffer;
			ScratchBufferDesc.BytesPerElement = GRHIRayTracingScratchBufferAlignment;
			ScratchBufferDesc.NumElements = FMath::DivideAndRoundUp(BLASScratchSize, GRHIRayTracingScratchBufferAlignment);

			FRDGBufferRef ScratchBuffer = GraphBuilder.CreateBuffer(ScratchBufferDesc, TEXT("NaniteRayTracing.BLASSharedScratchBuffer"));

			FNaniteRayTracingPrimitivesParams* PassParams = GraphBuilder.AllocParameters<FNaniteRayTracingPrimitivesParams>();
			PassParams->Buffer0 = nullptr;
			PassParams->Buffer1 = nullptr;
			PassParams->ScratchBuffer = ScratchBuffer;

			GraphBuilder.AddPass(RDG_EVENT_NAME("NaniteRayTracing::UpdateBLASes"), PassParams, ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				[PassParams, BuildParams = MoveTemp(BuildParams)](FRHIComputeCommandList& RHICmdList)
			{
				FRHIBufferRange ScratchBufferRange;
				ScratchBufferRange.Buffer = PassParams->ScratchBuffer->GetRHI();
				ScratchBufferRange.Offset = 0;
				RHICmdList.BuildAccelerationStructures(BuildParams, ScratchBufferRange);
			});

			bAnyBlasRebuilt = true;
		}

		if (ReadbackBuffersNumPending == 0 && PendingBuilds.IsEmpty())
		{
			bUpdating = false;
		}

		return bAnyBlasRebuilt;
	}

	FRHIRayTracingGeometry* FRayTracingManager::GetRayTracingGeometry(FPrimitiveSceneInfo* SceneInfo) const
	{
		auto NaniteProxy = static_cast<const Nanite::FSceneProxyBase*>(SceneInfo->Proxy);

		const uint32 Id = NaniteProxy->GetRayTracingId();

		if (Id == INDEX_NONE)
		{
			return nullptr;
		}

		const FInternalData* Data = Geometries[Id];

		return Data->RayTracingGeometryRHI;
	}

	bool FRayTracingManager::CheckModeChanged()
	{
		bPrevMode = bCurrentMode;
		bCurrentMode = GetRayTracingMode();
		return bPrevMode != bCurrentMode;
	}

	float FRayTracingManager::GetCutError() const
	{
		return GNaniteRayTracingCutError;
	}

	TGlobalResource<FRayTracingManager> GRayTracingManager;
} // namespace Nanite

#endif // RHI_RAYTRACING
