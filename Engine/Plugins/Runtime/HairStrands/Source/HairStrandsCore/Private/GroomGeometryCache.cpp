// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomGeometryCache.h"
#include "HairStrandsMeshProjection.h"

#include "GeometryCacheComponent.h"
#include "GeometryCacheMeshData.h"
#include "GeometryCacheSceneProxy.h"
#include "GPUSkinCache.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "CommonRenderResources.h"
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalRenderPublic.h"
#include "Rendering/SkeletalMeshLODRenderData.h"

///////////////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsProjectionMeshData::Section ConvertMeshSection(const FCachedGeometry::Section& In)
{
	FHairStrandsProjectionMeshData::Section Out;
	Out.IndexBuffer = In.IndexBuffer;
	Out.RDGPositionBuffer = In.RDGPositionBuffer;
	Out.PositionBuffer = In.PositionBuffer;
	Out.UVsBuffer = In.UVsBuffer;
	Out.UVsChannelOffset = In.UVsChannelOffset;
	Out.UVsChannelCount = In.UVsChannelCount;
	Out.TotalVertexCount = In.TotalVertexCount;
	Out.TotalIndexCount = In.TotalIndexCount;
	Out.VertexBaseIndex = In.VertexBaseIndex;
	Out.IndexBaseIndex = In.IndexBaseIndex;
	Out.NumPrimitives = In.NumPrimitives;
	Out.NumVertices= In.NumVertices;
	Out.SectionIndex = In.SectionIndex;
	Out.LODIndex = In.LODIndex;
	return Out;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void BuildBoneMatrices(USkeletalMeshComponent* SkeletalMeshComponent, const FSkeletalMeshLODRenderData& LODData,
	const uint32 LODIndex, TArray<uint32>& MatrixOffsets, TArray<FVector4>& BoneMatrices)
{
	TArray<FMatrix> BoneTransforms;
	SkeletalMeshComponent->GetCurrentRefToLocalMatrices(BoneTransforms, LODIndex);

	MatrixOffsets.SetNum(LODData.GetNumVertices());
	uint32 BonesOffset = 0;
	for (int32 SectionIdx = 0; SectionIdx < LODData.RenderSections.Num(); ++SectionIdx)
	{
		const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIdx];
		for (uint32 SectionVertex = 0; SectionVertex < Section.NumVertices; ++SectionVertex)
		{
			MatrixOffsets[Section.BaseVertexIndex + SectionVertex] = BonesOffset;
		}
		BonesOffset += Section.BoneMap.Num();
	}
	BoneMatrices.SetNum(BonesOffset * 3);
	BonesOffset = 0;
	for (int32 SectionIdx = 0; SectionIdx < LODData.RenderSections.Num(); ++SectionIdx)
	{
		const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIdx];
		for (int32 BoneIdx = 0; BoneIdx < Section.BoneMap.Num(); ++BoneIdx, ++BonesOffset)
		{
			BoneTransforms[Section.BoneMap[BoneIdx]].To3x4MatrixTranspose(&BoneMatrices[3 * BonesOffset].X);
		}
	}
}

 void BuildCacheGeometry(
	 FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap, 
	USkeletalMeshComponent* SkeletalMeshComponent, 
	FCachedGeometry& CachedGeometry)
{
	if (SkeletalMeshComponent)
	{
		FSkeletalMeshRenderData* RenderData = SkeletalMeshComponent->SkeletalMesh->GetResourceForRendering();

		const uint32 LODIndex = SkeletalMeshComponent->GetPredictedLODLevel();// RenderData->PendingFirstLODIdx;
		FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];

		TArray<uint32> MatrixOffsets;
		TArray<FVector4> BoneMatrices;
		BuildBoneMatrices(SkeletalMeshComponent, LODData, LODIndex, MatrixOffsets, BoneMatrices);

		FRDGBufferRef DeformedPositionsBuffer	= GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(float), LODData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices() * 3), TEXT("HairStrandsSkinnedDeformedPositions"));
		FRDGBufferRef BoneMatricesBuffer		= CreateStructuredBuffer(GraphBuilder, TEXT("HairStrandsSkinnedBoneMatrices"), sizeof(float) * 4, BoneMatrices.Num(), BoneMatrices.GetData(), sizeof(float) * 4 * BoneMatrices.Num());
		FRDGBufferRef MatrixOffsetsBuffer		= CreateStructuredBuffer(GraphBuilder, TEXT("HairStrandsSkinnedMatrixOffsets"), sizeof(uint32), MatrixOffsets.Num(), MatrixOffsets.GetData(), sizeof(uint32) * MatrixOffsets.Num());

		AddSkinUpdatePass(GraphBuilder, ShaderMap, SkeletalMeshComponent->GetSkinWeightBuffer(LODIndex), LODData, BoneMatricesBuffer, MatrixOffsetsBuffer, DeformedPositionsBuffer);

		CachedGeometry.DeformedPositionBuffer = DeformedPositionsBuffer;
		FRDGBufferSRVRef DeformedPositionSRV = GraphBuilder.CreateSRV(DeformedPositionsBuffer, PF_R32_FLOAT);

		for (int32 SectionIdx = 0; SectionIdx < LODData.RenderSections.Num(); ++SectionIdx)
		{
			FCachedGeometry::Section CachedSection;
			FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIdx];

			CachedSection.RDGPositionBuffer = DeformedPositionSRV;
			CachedSection.PositionBuffer = nullptr; // Do not use the SRV slot, but instead use the RDG buffer created above (DeformedPositionSRV)
			CachedSection.UVsBuffer = LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetTexCoordsSRV();
			CachedSection.TotalVertexCount = LODData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices();
			CachedSection.IndexBuffer = LODData.MultiSizeIndexContainer.GetIndexBuffer()->GetSRV();
			CachedSection.TotalIndexCount = LODData.MultiSizeIndexContainer.GetIndexBuffer()->Num();
			CachedSection.UVsChannelCount = LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
			CachedSection.NumPrimitives = Section.NumTriangles;
			CachedSection.NumVertices = Section.NumVertices;
			CachedSection.IndexBaseIndex = Section.BaseIndex;
			CachedSection.VertexBaseIndex = Section.BaseVertexIndex;
			CachedSection.SectionIndex = SectionIdx;
			CachedSection.LODIndex = LODIndex;
			CachedSection.UVsChannelOffset = 0; // Assume that we needs to pair meshes based on UVs 0

			CachedGeometry.Sections.Add(CachedSection);
		}
	}
}

 void BuildCacheGeometry(
	 FRDGBuilder& GraphBuilder,
	 FGlobalShaderMap* ShaderMap, 
	 UGeometryCacheComponent* GeometryCacheComponent, 
	 FCachedGeometry& CachedGeometry)
 {
	 if (GeometryCacheComponent)
	 {
		 if (FGeometryCacheSceneProxy* SceneProxy = static_cast<FGeometryCacheSceneProxy*>(GeometryCacheComponent->SceneProxy))
		 {
			 // Prior to getting here, the GeometryCache has been validated to be flattened (ie. has only one track)
			 check(SceneProxy->GetTracks().Num() > 0);
			 const FGeomCacheTrackProxy* TrackProxy = SceneProxy->GetTracks()[0];

			 check(TrackProxy->MeshData && TrackProxy->NextFrameMeshData);
			 const bool bHasMotionVectors = (
				 TrackProxy->MeshData->VertexInfo.bHasMotionVectors &&
				 TrackProxy->NextFrameMeshData->VertexInfo.bHasMotionVectors &&
				 TrackProxy->MeshData->Positions.Num() == TrackProxy->MeshData->MotionVectors.Num())
				 && (TrackProxy->NextFrameMeshData->Positions.Num() == TrackProxy->NextFrameMeshData->MotionVectors.Num());

			 // PositionBuffer depends on CurrentPositionBufferIndex and on if the cache has motion vectors
			 const uint32 PositionIndex = (TrackProxy->CurrentPositionBufferIndex == -1 || bHasMotionVectors) ? 0 : TrackProxy->CurrentPositionBufferIndex % 2;

			 for (int32 SectionIdx = 0; SectionIdx < TrackProxy->MeshData->BatchesInfo.Num(); ++SectionIdx)
			 {
				const FGeometryCacheMeshBatchInfo& BatchInfo = TrackProxy->MeshData->BatchesInfo[SectionIdx];
				
				FCachedGeometry::Section CachedSection;
				CachedSection.PositionBuffer = TrackProxy->PositionBuffers[PositionIndex].GetBufferSRV();
				CachedSection.UVsBuffer = TrackProxy->TextureCoordinatesBuffer.GetBufferSRV();
				CachedSection.TotalVertexCount = TrackProxy->MeshData->Positions.Num();
				// It is unclear why the existing SRV is invalid. When the index buffer is changed/recreated its SRV is supposed to be update.
				//CachedSection.IndexBuffer = TrackProxy->IndexBuffer.GetBufferSRV();
				if (TrackProxy->IndexBuffer.IsInitialized())
				{
					CachedSection.IndexBuffer = RHICreateShaderResourceView(TrackProxy->IndexBuffer.IndexBufferRHI);
				}
				CachedSection.TotalIndexCount = TrackProxy->IndexBuffer.NumIndices;
				CachedSection.UVsChannelCount = 1;
				CachedSection.NumPrimitives = BatchInfo.NumTriangles;
				CachedSection.NumVertices = TrackProxy->MeshData->Positions.Num();
				CachedSection.IndexBaseIndex = BatchInfo.StartIndex;
				CachedSection.VertexBaseIndex = 0;
				CachedSection.SectionIndex = SectionIdx;
				CachedSection.LODIndex = 0;
				CachedSection.UVsChannelOffset = 0;
				
				if (CachedSection.PositionBuffer && CachedSection.IndexBuffer)
				{
					CachedGeometry.Sections.Add(CachedSection);
				}
			 }
		 }
	 }
 }
