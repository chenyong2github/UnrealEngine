// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomGeometryCache.h"
#include "HairStrandsMeshProjection.h"

#include "GeometryCacheComponent.h"
#include "GeometryCacheMeshData.h"
#include "GeometryCacheSceneProxy.h"
#include "CachedGeometry.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "CommonRenderResources.h"
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalRenderPublic.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "RenderGraphUtils.h"

///////////////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsProjectionMeshData::Section ConvertMeshSection(FCachedGeometry const& InCachedGeometry, int32 InSectionIndex)
{
	FCachedGeometry::Section const& In = InCachedGeometry.Sections[InSectionIndex];
	FHairStrandsProjectionMeshData::Section Out;
	Out.IndexBuffer = In.IndexBuffer;
	Out.RDGPositionBuffer = In.RDGPositionBuffer;
	Out.RDGPreviousPositionBuffer = In.RDGPreviousPositionBuffer;
	Out.PositionBuffer = In.PositionBuffer;
	Out.PreviousPositionBuffer = In.PreviousPositionBuffer;
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
	Out.LocalToWorld = InCachedGeometry.LocalToWorld;
	return Out;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ENGINE_API void UpdateRefToLocalMatrices(TArray<FMatrix44f>& ReferenceToLocal, const USkinnedMeshComponent* InMeshComponent, const FSkeletalMeshRenderData* InSkeletalMeshRenderData, int32 LODIndex, const TArray<FBoneIndexType>* ExtraRequiredBoneIndices = NULL);
ENGINE_API void UpdatePreviousRefToLocalMatrices(TArray<FMatrix44f>& ReferenceToLocal, const USkinnedMeshComponent* InMeshComponent, const FSkeletalMeshRenderData* InSkeletalMeshRenderData, int32 LODIndex, const TArray<FBoneIndexType>* ExtraRequiredBoneIndices = NULL);

static void BuildBoneMatrices(
	const USkeletalMeshComponent* SkeletalMeshComponent, 
	const FSkeletalMeshRenderData* RenderData,
	const FSkeletalMeshLODRenderData& LODData,
	const uint32 LODIndex, 
	const bool bNeedPreviousPosition,
	TArray<uint32>& OutMatrixOffsets, 
	TArray<FVector4f>& OutBoneMatrices,
	TArray<FVector4f>& OutPrevBoneMatrices)
{
	//SkeletalMeshComponent->GetCurrentRefToLocalMatrices(BoneTransforms, LODIndex);
	check(RenderData->LODRenderData.IsValidIndex(LODIndex));

	TArray<FMatrix44f> BoneTransforms;
	UpdateRefToLocalMatrices(BoneTransforms, SkeletalMeshComponent, RenderData, LODIndex, nullptr);

	TArray<FMatrix44f> PrevBoneTransforms;
	if (bNeedPreviousPosition)
	{
		UpdatePreviousRefToLocalMatrices(PrevBoneTransforms, SkeletalMeshComponent, RenderData, LODIndex, nullptr);
	}
	
	OutMatrixOffsets.SetNum(LODData.GetNumVertices());
	uint32 BonesOffset = 0;
	for (int32 SectionIdx = 0; SectionIdx < LODData.RenderSections.Num(); ++SectionIdx)
	{
		const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIdx];
		for (uint32 SectionVertex = 0; SectionVertex < Section.NumVertices; ++SectionVertex)
		{
			OutMatrixOffsets[Section.BaseVertexIndex + SectionVertex] = BonesOffset;
		}
		BonesOffset += Section.BoneMap.Num();
	}
	OutBoneMatrices.SetNum(BonesOffset * 3);
	OutPrevBoneMatrices.SetNum(BonesOffset * 3);
	BonesOffset = 0;
	for (int32 SectionIdx = 0; SectionIdx < LODData.RenderSections.Num(); ++SectionIdx)
	{
		const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIdx];
		for (int32 BoneIdx = 0; BoneIdx < Section.BoneMap.Num(); ++BoneIdx, ++BonesOffset)
		{
			BoneTransforms[Section.BoneMap[BoneIdx]].To3x4MatrixTranspose(&OutBoneMatrices[3 * BonesOffset].X);
			if (bNeedPreviousPosition)
			{
				PrevBoneTransforms[Section.BoneMap[BoneIdx]].To3x4MatrixTranspose(&OutPrevBoneMatrices[3 * BonesOffset].X);
			}
		}
	}
}

 void BuildCacheGeometry(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap, 
	const USkeletalMeshComponent* SkeletalMeshComponent, 
	const bool bOutputTriangleData,
	FCachedGeometry& Out)
{
	if (SkeletalMeshComponent)
	{
		const uint32 LODIndex = SkeletalMeshComponent->GetPredictedLODLevel();// RenderData->PendingFirstLODIdx;
		Out.LocalToWorld = SkeletalMeshComponent->SceneProxy ? FTransform(SkeletalMeshComponent->SceneProxy->GetLocalToWorld()) : FTransform();
		Out.LODIndex = LODIndex;

		if (bOutputTriangleData)
		{
			FSkeletalMeshRenderData* RenderData = SkeletalMeshComponent->GetSkeletalMesh()->GetResourceForRendering();
			FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];

			const bool bNeedPreviousPosition = IsHairStrandContinuousDecimationReorderingEnabled();

			TArray<uint32> MatrixOffsets;
			TArray<FVector4f> BoneMatrices;
			TArray<FVector4f> PrevBoneMatrices;
			BuildBoneMatrices(SkeletalMeshComponent, RenderData, LODData, LODIndex, bNeedPreviousPosition, MatrixOffsets, BoneMatrices, PrevBoneMatrices);

			FRDGBufferRef DeformedPositionsBuffer			= GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(float), LODData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices() * 3), TEXT("Hair.SkinnedDeformedPositions"));
			FRDGBufferRef DeformedPreviousPositionsBuffer	= bNeedPreviousPosition ? GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(float), LODData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices() * 3), TEXT("Hair.SkinnedDeformedPreviousPositions")) : nullptr;
			FRDGBufferRef BoneMatricesBuffer				= CreateStructuredBuffer(GraphBuilder, TEXT("Hair.SkinnedBoneMatrices"), sizeof(float) * 4, BoneMatrices.Num(), BoneMatrices.GetData(), sizeof(float) * 4 * BoneMatrices.Num());
			FRDGBufferRef PreviousBoneMatricesBuffer		= bNeedPreviousPosition ? CreateStructuredBuffer(GraphBuilder, TEXT("Hair.SkinnedPreviousBoneMatrices"), sizeof(float) * 4, BoneMatrices.Num(), BoneMatrices.GetData(), sizeof(float) * 4 * BoneMatrices.Num()) : nullptr;
			FRDGBufferRef MatrixOffsetsBuffer				= CreateStructuredBuffer(GraphBuilder, TEXT("Hair.SkinnedMatrixOffsets"), sizeof(uint32), MatrixOffsets.Num(), MatrixOffsets.GetData(), sizeof(uint32) * MatrixOffsets.Num());

			AddSkinUpdatePass(GraphBuilder, ShaderMap, SkeletalMeshComponent->GetSkinWeightBuffer(LODIndex), LODData, BoneMatricesBuffer, PreviousBoneMatricesBuffer, MatrixOffsetsBuffer, DeformedPositionsBuffer, DeformedPreviousPositionsBuffer);

			Out.DeformedPositionBuffer = DeformedPositionsBuffer;
			Out.DeformedPreviousPositionBuffer = DeformedPreviousPositionsBuffer;
			FRDGBufferSRVRef DeformedPositionSRV = GraphBuilder.CreateSRV(DeformedPositionsBuffer, PF_R32_FLOAT);
			FRDGBufferSRVRef DeformedPreviousPositionSRV = bNeedPreviousPosition ? GraphBuilder.CreateSRV(DeformedPreviousPositionsBuffer, PF_R32_FLOAT) : nullptr;
			for (int32 SectionIdx = 0; SectionIdx < LODData.RenderSections.Num(); ++SectionIdx)
			{
				const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIdx];

				FCachedGeometry::Section& OutSection = Out.Sections.AddDefaulted_GetRef();
				OutSection.RDGPositionBuffer = DeformedPositionSRV;
				OutSection.RDGPreviousPositionBuffer = DeformedPreviousPositionSRV;
				OutSection.PositionBuffer = nullptr; // Do not use the SRV slot, but instead use the RDG buffer created above (DeformedPositionSRV)
				OutSection.PreviousPositionBuffer = nullptr; // Do not use the SRV slot, but instead use the RDG buffer created above (DeformedPositionSRV)
				OutSection.UVsBuffer = LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetTexCoordsSRV();
				OutSection.TotalVertexCount = LODData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices();
				OutSection.IndexBuffer = LODData.MultiSizeIndexContainer.GetIndexBuffer()->GetSRV();
				OutSection.TotalIndexCount = LODData.MultiSizeIndexContainer.GetIndexBuffer()->Num();
				OutSection.UVsChannelCount = LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
				OutSection.NumPrimitives = Section.NumTriangles;
				OutSection.NumVertices = Section.NumVertices;
				OutSection.IndexBaseIndex = Section.BaseIndex;
				OutSection.VertexBaseIndex = Section.BaseVertexIndex;
				OutSection.SectionIndex = SectionIdx;
				OutSection.LODIndex = LODIndex;
				OutSection.UVsChannelOffset = 0; // Assume that we needs to pair meshes based on UVs 0
			}
		}
	}
}

 void BuildCacheGeometry(
	 FRDGBuilder& GraphBuilder,
	 FGlobalShaderMap* ShaderMap, 
	 const UGeometryCacheComponent* GeometryCacheComponent, 
	 const bool bOutputTriangleData,
	 FCachedGeometry& Out)
 {
	 if (GeometryCacheComponent)
	 {
		 if (FGeometryCacheSceneProxy* SceneProxy = static_cast<FGeometryCacheSceneProxy*>(GeometryCacheComponent->SceneProxy))
		 {
			 Out.LocalToWorld = FTransform(SceneProxy->GetLocalToWorld());
			 Out.LODIndex = 0;
			 if (bOutputTriangleData)
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
				
					FCachedGeometry::Section OutSection;
					OutSection.PositionBuffer = TrackProxy->PositionBuffers[PositionIndex].GetBufferSRV();
					OutSection.UVsBuffer = TrackProxy->TextureCoordinatesBuffer.GetBufferSRV();
					OutSection.TotalVertexCount = TrackProxy->MeshData->Positions.Num();
					OutSection.IndexBuffer = TrackProxy->IndexBuffer.GetBufferSRV();
					OutSection.TotalIndexCount = TrackProxy->IndexBuffer.NumValidIndices;
					OutSection.UVsChannelCount = 1;
					OutSection.NumPrimitives = BatchInfo.NumTriangles;
					OutSection.NumVertices = TrackProxy->MeshData->Positions.Num();
					OutSection.IndexBaseIndex = BatchInfo.StartIndex;
					OutSection.VertexBaseIndex = 0;
					OutSection.SectionIndex = SectionIdx;
					OutSection.LODIndex = 0;
					OutSection.UVsChannelOffset = 0;
				
					if (OutSection.PositionBuffer && OutSection.IndexBuffer)
					{
						Out.Sections.Add(OutSection);
					}
				 }
			 }
		 }
	 }
 }
