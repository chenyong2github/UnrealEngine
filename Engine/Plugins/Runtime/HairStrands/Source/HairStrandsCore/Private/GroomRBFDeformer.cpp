// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomRBFDeformer.h"
#include "Async/ParallelFor.h"
#include "GroomAsset.h"
#include "GroomBindingAsset.h"
#include "GroomBuilder.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "MeshAttributes.h"
#include "Engine/StaticMesh.h"

// HairStrandsSamplesInit.usf
void InitMeshSamples(
	uint32 MaxVertexCount,
	const TArray<FVector>& VertexPositionsBuffer,
	uint32 MaxSampleCount,
	const TArray<uint32>& SampleIndicesBuffer,
	TArray<FVector>& OutSamplePositionsBuffer
)
{
	OutSamplePositionsBuffer.SetNum(MaxSampleCount);
	for (uint32 SampleIndex = 0; SampleIndex < MaxSampleCount; ++SampleIndex)
	{
		const uint32 VertexIndex = SampleIndicesBuffer[SampleIndex];
		if (VertexIndex >= MaxVertexCount)
			continue;

		OutSamplePositionsBuffer[SampleIndex] = VertexPositionsBuffer[VertexIndex];
	}
}

// HairStrandsSamplesUpdate.usf
void UpdateMeshSamples(
	uint32 MaxSampleCount,
	const TArray<float>& InterpolationWeightsBuffer,
	const TArray<FVector4>& SampleRestPositionsBuffer,
	const TArray<FVector>& SampleDeformedPositionsBuffer,
	TArray<FVector>& OutSampleDeformationsBuffer
)
{
	OutSampleDeformationsBuffer.SetNum(MaxSampleCount + 5);
	OutSampleDeformationsBuffer[MaxSampleCount + 4] = SampleDeformedPositionsBuffer[0];

	for (uint32 SampleIndex = 0; SampleIndex < MaxSampleCount + 4; ++SampleIndex)
	{
		uint32 WeightsOffset = SampleIndex * (MaxSampleCount + 4);
		FVector SampleDeformation(FVector::ZeroVector);
		for (uint32 i = 0; i < MaxSampleCount; ++i, ++WeightsOffset)
		{
			SampleDeformation += InterpolationWeightsBuffer[WeightsOffset] *
				(SampleDeformedPositionsBuffer[i] - (SampleRestPositionsBuffer[i] + SampleDeformedPositionsBuffer[0]) );
		}

		OutSampleDeformationsBuffer[SampleIndex] = SampleDeformation;
	}
}

// HairStrandsGuideDeform.usf
FVector DisplacePosition(
	const FVector& RestControlPoint,
	uint32 SampleCount,
	const TArray<FVector4>& RestSamplePositionsBuffer,
	const TArray<FVector>& MeshSampleWeightsBuffer
)
{
	FVector ControlPoint = RestControlPoint;

	// Apply rbf interpolation from the samples set
	for (uint32 i = 0; i < SampleCount; ++i)
	{
		const FVector PositionDelta = RestControlPoint - RestSamplePositionsBuffer[i];
		const float FunctionValue = FMath::Sqrt(FVector::DotProduct(PositionDelta, PositionDelta) + 1);
		ControlPoint += FunctionValue * MeshSampleWeightsBuffer[i];
	}
	ControlPoint += MeshSampleWeightsBuffer[SampleCount];
	ControlPoint += MeshSampleWeightsBuffer[SampleCount + 1] * RestControlPoint.X;
	ControlPoint += MeshSampleWeightsBuffer[SampleCount + 2] * RestControlPoint.Y;
	ControlPoint += MeshSampleWeightsBuffer[SampleCount + 3] * RestControlPoint.Z;
	return ControlPoint + MeshSampleWeightsBuffer[SampleCount + 4];
}

void DeformStrands(	
	const FHairStrandsDatas& HairStandsData,
	const TArray<FHairStrandsIndexFormat::Type>& VertexToCurveIndexBuffer,
	const TArray<FHairStrandsCurveTriangleBarycentricFormat::Type>& RootTriangleBarycentricBuffer,

	const TArray<FHairStrandsMeshTrianglePositionFormat::Type>& RootTrianglePosition0Buffer_Rest,
	const TArray<FHairStrandsMeshTrianglePositionFormat::Type>& RootTrianglePosition1Buffer_Rest,
	const TArray<FHairStrandsMeshTrianglePositionFormat::Type>& RootTrianglePosition2Buffer_Rest,

	const TArray<FHairStrandsMeshTrianglePositionFormat::Type>& RootTrianglePosition0Buffer_Deformed,
	const TArray<FHairStrandsMeshTrianglePositionFormat::Type>& RootTrianglePosition1Buffer_Deformed,
	const TArray<FHairStrandsMeshTrianglePositionFormat::Type>& RootTrianglePosition2Buffer_Deformed,

	uint32 VertexCount,
	uint32 SampleCount,
	const TArray<FVector>& RestPosePositionBuffer,
	const TArray<FVector4>& RestSamplePositionsBuffer,
	const TArray<FVector>& MeshSampleWeightsBuffer,
	TArray<FVector>& OutDeformedPositionBuffer
)
{
	// Raw deformation with RBF
	OutDeformedPositionBuffer.SetNum(RestPosePositionBuffer.Num());
	ParallelFor(VertexCount, [&](uint32 VertexIndex)
	{
		const FVector& ControlPoint = RestPosePositionBuffer[VertexIndex];
		const FVector DisplacedPosition = DisplacePosition(ControlPoint, SampleCount, RestSamplePositionsBuffer, MeshSampleWeightsBuffer);
		OutDeformedPositionBuffer[VertexIndex] = DisplacedPosition;
	});

	// Compute correction for snapping the strands back to the surface, as the RBF introduces low frequency offset
	const uint32 CurveCount = HairStandsData.GetNumCurves();
	TArray<FVector4> CorrectionOffsets;
	CorrectionOffsets.SetNum(CurveCount);
	ParallelFor(CurveCount, [&](uint32 CurveIndex)
	{
		const uint32 VertexOffset = HairStandsData.StrandsCurves.CurvesOffset[CurveIndex];
		const uint32 RootIndex = VertexToCurveIndexBuffer[VertexOffset];

		// Sanity check
		check(RootIndex == CurveIndex);

		const FVector& Rest_Position   = HairStandsData.StrandsPoints.PointsPosition[VertexOffset];
		const FVector& Deform_Position = OutDeformedPositionBuffer[VertexOffset];


		const uint32 PackedBarycentric = RootTriangleBarycentricBuffer[RootIndex];
		const FVector2D B0 = FHairStrandsRootUtils::DecodeBarycentrics(PackedBarycentric);
		const FVector   B  = FVector(B0.X, B0.Y, 1.f - B0.X - B0.Y);

		/* Strand hair roots translation and rotation in rest position relative to the bound triangle. Positions are relative to the rest root center */
		const FVector& Rest_V0 = RootTrianglePosition0Buffer_Rest[RootIndex];
		const FVector& Rest_V1 = RootTrianglePosition1Buffer_Rest[RootIndex];
		const FVector& Rest_V2 = RootTrianglePosition2Buffer_Rest[RootIndex];

		const FVector& Deform_V0 = RootTrianglePosition0Buffer_Deformed[RootIndex];
		const FVector& Deform_V1 = RootTrianglePosition1Buffer_Deformed[RootIndex];
		const FVector& Deform_V2 = RootTrianglePosition2Buffer_Deformed[RootIndex];

		const FVector Rest_RootPosition		=   Rest_V0 * B.X +   Rest_V1 * B.Y +   Rest_V2 * B.Z;
		const FVector Deform_RootPosition	= Deform_V0 * B.X + Deform_V1 * B.Y + Deform_V2 * B.Z;

		const FVector RestOffset = Rest_Position - Rest_RootPosition;
		const FVector SnappedDeformPosition = Deform_RootPosition + RestOffset;
		const FVector CorrectionOffset = SnappedDeformPosition - Deform_Position;

		CorrectionOffsets[CurveIndex] = FVector4(CorrectionOffset, 0);
	});

	// Apply correction offset to each control points
	ParallelFor(VertexCount, [&](uint32 VertexIndex)
	{
		const uint32 CurveIndex = VertexToCurveIndexBuffer[VertexIndex];
		const FVector4 CorrectionOffset = CorrectionOffsets[CurveIndex];
		OutDeformedPositionBuffer[VertexIndex] += CorrectionOffset;
	});
}

// Compute the triangle positions for each curve's roots
void ExtractRootTrianglePositions(
	const FHairStrandsRootData::FMeshProjectionLOD& RestLODData,
	const uint32 MeshLODIndex,
	const FHairStrandsDatas& HairStrandsData,
	const FSkeletalMeshRenderData* InMeshRenderData, 
	TArray<FHairStrandsMeshTrianglePositionFormat::Type>& OutDeformRootTrianglePosition0Buffer,
	TArray<FHairStrandsMeshTrianglePositionFormat::Type>& OutDeformRootTrianglePosition1Buffer,
	TArray<FHairStrandsMeshTrianglePositionFormat::Type>& OutDeformRootTrianglePosition2Buffer)
{
	const uint32 RootCount = HairStrandsData.GetNumCurves();
	OutDeformRootTrianglePosition0Buffer.SetNum(RootCount);
	OutDeformRootTrianglePosition1Buffer.SetNum(RootCount);
	OutDeformRootTrianglePosition2Buffer.SetNum(RootCount);

	const uint32 SectionCount = InMeshRenderData->LODRenderData[MeshLODIndex].RenderSections.Num();
	TArray<uint32> IndexBuffer;
	InMeshRenderData->LODRenderData[MeshLODIndex].MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);

	for (uint32 RootIndex = 0; RootIndex < RootCount; ++RootIndex)
	{
		const uint32 PackedTriangleIndex = RestLODData.RootTriangleIndexBuffer[RootIndex];
		uint32 TriangleIndex = 0;
		uint32 SectionIndex = 0;
		FHairStrandsRootUtils::DecodeTriangleIndex(PackedTriangleIndex, TriangleIndex, SectionIndex);

		check(SectionIndex < SectionCount)
		const uint32 TriangleCount = InMeshRenderData->LODRenderData[MeshLODIndex].RenderSections[SectionIndex].NumTriangles;
		const uint32 SectionBaseIndex = InMeshRenderData->LODRenderData[MeshLODIndex].RenderSections[SectionIndex].BaseIndex;

		const uint32 I0 = IndexBuffer[SectionBaseIndex + TriangleIndex * 3 + 0];
		const uint32 I1 = IndexBuffer[SectionBaseIndex + TriangleIndex * 3 + 1];
		const uint32 I2 = IndexBuffer[SectionBaseIndex + TriangleIndex * 3 + 2];

		const FVector P0 = InMeshRenderData->LODRenderData[MeshLODIndex].StaticVertexBuffers.PositionVertexBuffer.VertexPosition(I0);
		const FVector P1 = InMeshRenderData->LODRenderData[MeshLODIndex].StaticVertexBuffers.PositionVertexBuffer.VertexPosition(I1);
		const FVector P2 = InMeshRenderData->LODRenderData[MeshLODIndex].StaticVertexBuffers.PositionVertexBuffer.VertexPosition(I2);

		OutDeformRootTrianglePosition0Buffer[RootIndex] = P0;
		OutDeformRootTrianglePosition1Buffer[RootIndex] = P1;
		OutDeformRootTrianglePosition2Buffer[RootIndex] = P2;
	}
}

TArray<FVector> GetDeformedHairStrandsPositions(
	const TArray<FVector>& MeshVertexPositionsBuffer_Target,
	const FHairStrandsDatas& HairStrandsData,
	const uint32 MeshLODIndex,
	const FSkeletalMeshRenderData* InMeshRenderData,
	const TArray<FHairStrandsIndexFormat::Type>& VertexToCurveIndexBuffer,
	const FHairStrandsRootData::FMeshProjectionLOD& RestLODData)
{
	// Init the mesh samples with the target mesh vertices
	const int32 MaxVertexCount = MeshVertexPositionsBuffer_Target.Num();
	const uint32 MaxSampleCount = RestLODData.SampleCount;
	const TArray<uint32>& SampleIndicesBuffer = RestLODData.MeshSampleIndicesBuffer;
	TArray<FVector> OutSamplePositionsBuffer;

	InitMeshSamples(MaxVertexCount, MeshVertexPositionsBuffer_Target, MaxSampleCount, SampleIndicesBuffer, OutSamplePositionsBuffer);

	// Update those vertices with the RBF interpolation weights
	const TArray<float>& InterpolationWeightsBuffer = RestLODData.MeshInterpolationWeightsBuffer;
	const TArray<FVector4>& SampleRestPositionsBuffer = RestLODData.RestSamplePositionsBuffer;
	const TArray<FVector>& SampleDeformedPositionsBuffer = OutSamplePositionsBuffer;
	TArray<FVector> OutSampleDeformationsBuffer;

	UpdateMeshSamples(MaxSampleCount, InterpolationWeightsBuffer, SampleRestPositionsBuffer, SampleDeformedPositionsBuffer, OutSampleDeformationsBuffer);

	// Get the strands vertices positions centered at their bounding box
	const FHairStrandsPoints& Points = HairStrandsData.StrandsPoints;
	const uint32 VertexCount = Points.Num();

	TArray<FVector> OutPositions = Points.PointsPosition;

	// Use the vertex position of the binding, as the source asset might not have the same topology (in case the groom has been transfered from one mesh toanother using UV sharing)
	TArray<FHairStrandsMeshTrianglePositionFormat::Type> RootTrianglePosition0Buffer_Rest = RestLODData.RestRootTrianglePosition0Buffer;
	TArray<FHairStrandsMeshTrianglePositionFormat::Type> RootTrianglePosition1Buffer_Rest = RestLODData.RestRootTrianglePosition1Buffer;
	TArray<FHairStrandsMeshTrianglePositionFormat::Type> RootTrianglePosition2Buffer_Rest = RestLODData.RestRootTrianglePosition2Buffer;

	TArray<FHairStrandsMeshTrianglePositionFormat::Type> RootTrianglePosition0Buffer_Deformed;
	TArray<FHairStrandsMeshTrianglePositionFormat::Type> RootTrianglePosition1Buffer_Deformed;
	TArray<FHairStrandsMeshTrianglePositionFormat::Type> RootTrianglePosition2Buffer_Deformed;
	ExtractRootTrianglePositions(
		RestLODData,
		MeshLODIndex,
		HairStrandsData,
		InMeshRenderData,
		RootTrianglePosition0Buffer_Deformed,
		RootTrianglePosition1Buffer_Deformed,
		RootTrianglePosition2Buffer_Deformed);

	// Deform the strands vertices with the deformed mesh samples
	const TArray<FVector>& RestPosePositionBuffer = OutPositions;
	const TArray<FVector4>& RestSamplePositionsBuffer = RestLODData.RestSamplePositionsBuffer;
	const TArray<FVector>& MeshSampleWeightsBuffer = OutSampleDeformationsBuffer;
	TArray<FVector> OutDeformedPositionBuffer;

	DeformStrands(
		HairStrandsData,
		VertexToCurveIndexBuffer,
		RestLODData.RootTriangleBarycentricBuffer,

		RootTrianglePosition0Buffer_Rest,
		RootTrianglePosition1Buffer_Rest,
		RootTrianglePosition2Buffer_Rest,

		RootTrianglePosition0Buffer_Deformed,
		RootTrianglePosition1Buffer_Deformed,
		RootTrianglePosition2Buffer_Deformed,

		VertexCount, 
		MaxSampleCount, 
		RestPosePositionBuffer, 
		RestSamplePositionsBuffer, 
		MeshSampleWeightsBuffer, 
		OutDeformedPositionBuffer);

	return MoveTemp(OutDeformedPositionBuffer);
}

struct FRBFDeformedPositions 
{
	TArray<FVector> RenderStrands;
	TArray<FVector> GuideStrands;
};

#if WITH_EDITORONLY_DATA
static void ApplyDeformationToGroom(const TArray<FRBFDeformedPositions>& DeformedPositions, UGroomAsset* GroomAsset)
{
	// The deformation must be stored in the HairDescription to rebuild the hair data when the groom is loaded
	FHairDescription HairDescription = GroomAsset->GetHairDescription();

	// Strands attributes as inputs
	TStrandAttributesConstRef<int> StrandNumVertices = HairDescription.StrandAttributes().GetAttributesRef<int>(HairAttribute::Strand::VertexCount);
	TStrandAttributesConstRef<int> StrandGuides = HairDescription.StrandAttributes().GetAttributesRef<int>(HairAttribute::Strand::Guide);
	TStrandAttributesConstRef<int> GroupIDs = HairDescription.StrandAttributes().GetAttributesRef<int>(HairAttribute::Strand::GroupID);

	// Guide and GroupID attributes are optional so must ensure they are available before using them
	bool bHasGuides = StrandGuides.IsValid();
	bool bHasGroupIDs = GroupIDs.IsValid();

	// We have to keep the same ordering of guides/strands and groups of vertices so that the deformed positions
	// are output in the right order in the HairDescription.
	// Thus, the deformed positions will be flattened into a single array
	struct FGroupInfo
	{
		// For debugging
		int32 NumRenderVertices = 0;
		int32 NumGuideVertices = 0;

		// For flattening the vertices into the array
		int32 CurrentRenderVertexIndex = 0;
		int32 CurrentGuideVertexIndex = 0;
	};

	TArray<FGroupInfo> GroupInfos;
	GroupInfos.SetNum(DeformedPositions.Num());

	const int32 GroomNumVertices = HairDescription.GetNumVertices();
	TArray<FVector> FlattenedDeformedPositions;
	FlattenedDeformedPositions.Reserve(GroomNumVertices);

	// Mapping of GroupID to GroupIndex to preserver ordering
	TMap<int32, int32> GroupIDToGroupIndex;

	const int32 NumStrands = HairDescription.GetNumStrands();
	for (int32 StrandIndex = 0; StrandIndex < NumStrands; ++StrandIndex)
	{
		FStrandID StrandID(StrandIndex);

		// Determine the group index to get the deformed positions from based on the strand group ID
		const int32 GroupID = bHasGroupIDs ? GroupIDs[StrandID] : 0;
		int32 GroupIndex = 0;
		int32* GroupIndexPtr = GroupIDToGroupIndex.Find(GroupID);
		if (GroupIndexPtr)
		{
			GroupIndex = *GroupIndexPtr;
		}
		else
		{
			GroupIndex = GroupIDToGroupIndex.Add(GroupID, GroupIDToGroupIndex.Num());
		}

		FGroupInfo& GroupInfo = GroupInfos[GroupIndex];

		// Determine the strand type: guide or render
		// Then, flattened the vertices positions from the selected group and strand type
		const int32 StrandGuide = bHasGuides ? StrandGuides[StrandID] : 0;
		const int32 NumVertices = StrandNumVertices[StrandID];
		if (StrandGuide > 0)
		{
			GroupInfo.NumGuideVertices += NumVertices;
			for (int32 Index = 0; Index < NumVertices; ++Index)
			{
				if (GroupInfo.CurrentGuideVertexIndex < DeformedPositions[GroupIndex].GuideStrands.Num())
				{
					FlattenedDeformedPositions.Add(DeformedPositions[GroupIndex].GuideStrands[GroupInfo.CurrentGuideVertexIndex++]);
				}
			}
		}
		else
		{
			GroupInfo.NumRenderVertices += NumVertices;
			for (int32 Index = 0; Index < NumVertices; ++Index)
			{
				if (GroupInfo.CurrentRenderVertexIndex < DeformedPositions[GroupIndex].RenderStrands.Num())
				{
					FlattenedDeformedPositions.Add(DeformedPositions[GroupIndex].RenderStrands[GroupInfo.CurrentRenderVertexIndex++]);
				}
			}
		}
	}

	// Output the flattened deformed positions into the HairDescription
	TVertexAttributesRef<FVector> VertexPositions = HairDescription.VertexAttributes().GetAttributesRef<FVector>(HairAttribute::Vertex::Position);
	for (int32 VertexIndex = 0; VertexIndex < GroomNumVertices; ++VertexIndex)
	{
		FVertexID VertexID(VertexIndex);
		VertexPositions[VertexID] = FlattenedDeformedPositions[VertexIndex];
	}

	FGroomBuilder::BuildGroom(HairDescription, GroomAsset);
	FGroomBuilder::BuildClusterData(GroomAsset, FGroomBuilder::ComputeGroomBoundRadius(GroomAsset->HairGroupsData));
	
	GroomAsset->CommitHairDescription(MoveTemp(HairDescription));
	GroomAsset->UpdateHairGroupsInfo();

	// Update/reimport the cards/meshes geometry which have been deformed prior to call this function
	GroomAsset->BuildCardsGeometry();
	GroomAsset->BuildMeshesGeometry();
}

static void ExtractSkeletalVertexPosition(
	const FSkeletalMeshRenderData* SkeletalMeshData,
	const uint32 MeshLODIndex,
	TArray<FVector>& OutMeshVertexPositionsBuffer)
{	
	const uint32 VertexCount = SkeletalMeshData->LODRenderData[MeshLODIndex].StaticVertexBuffers.PositionVertexBuffer.GetNumVertices();
	OutMeshVertexPositionsBuffer.SetNum(VertexCount);
	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		OutMeshVertexPositionsBuffer[VertexIndex] = SkeletalMeshData->LODRenderData[MeshLODIndex].StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex);
	}
}

void DeformStaticMeshPositions(
	UStaticMesh* OutMesh, 
	const TArray<FVector>& MeshVertexPositionsBuffer_Target,
	const FHairStrandsRootData::FMeshProjectionLOD& RestLODData)
{
	// Init the mesh samples with the target mesh vertices
	const int32 MaxVertexCount = MeshVertexPositionsBuffer_Target.Num();
	const uint32 MaxSampleCount = RestLODData.SampleCount;
	const TArray<uint32>& SampleIndicesBuffer = RestLODData.MeshSampleIndicesBuffer;
	TArray<FVector> OutSamplePositionsBuffer;

	InitMeshSamples(MaxVertexCount, MeshVertexPositionsBuffer_Target, MaxSampleCount, SampleIndicesBuffer, OutSamplePositionsBuffer);

	// Update those vertices with the RBF interpolation weights
	const TArray<float>& InterpolationWeightsBuffer = RestLODData.MeshInterpolationWeightsBuffer;
	const TArray<FVector4>& SampleRestPositionsBuffer = RestLODData.RestSamplePositionsBuffer;
	const TArray<FVector>& SampleDeformedPositionsBuffer = OutSamplePositionsBuffer;
	TArray<FVector> OutSampleDeformationsBuffer;

	UpdateMeshSamples(MaxSampleCount, InterpolationWeightsBuffer, SampleRestPositionsBuffer, SampleDeformedPositionsBuffer, OutSampleDeformationsBuffer);

	// Deform the strands vertices with the deformed mesh samples
	const TArray<FVector4>& RestSamplePositionsBuffer = RestLODData.RestSamplePositionsBuffer;
	const TArray<FVector>& MeshSampleWeightsBuffer = OutSampleDeformationsBuffer;

	// Raw deformation with RBF
	const uint32 MeshLODCount = OutMesh->GetNumLODs();
	TArray<const FMeshDescription*> MeshDescriptions;
	for (uint32 MeshLODIt = 0; MeshLODIt < MeshLODCount; ++MeshLODIt)
	{
		FMeshDescription* MeshDescription = OutMesh->GetMeshDescription(MeshLODIt);
		TVertexAttributesRef<FVector> VertexPositions = MeshDescription->VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);

		const uint32 VertexCount = VertexPositions.GetNumElements();
		ParallelFor(VertexCount, [&](uint32 VertexIndex)
		{
			FVertexID VertexID(VertexIndex);
			FVector& Point = VertexPositions[VertexID];
			Point = DisplacePosition(Point, MaxSampleCount, RestSamplePositionsBuffer, MeshSampleWeightsBuffer);
		});
		MeshDescriptions.Add(MeshDescription);
	}
	OutMesh->BuildFromMeshDescriptions(MeshDescriptions);
}

namespace GroomDerivedDataCacheUtils
{
	FString BuildCardsDerivedDataKeySuffix(uint32 GroupIndex, const TArray<FHairLODSettings>& LODs, TArray<FHairGroupsCardsSourceDescription>& SourceDescriptions);
}
#endif // #if WITH_EDITORONLY_DATA

void FGroomRBFDeformer::GetRBFDeformedGroomAsset(const UGroomAsset* InGroomAsset, const UGroomBindingAsset* BindingAsset, UGroomAsset* OutGroomAsset)
{
#if WITH_EDITORONLY_DATA
	if (InGroomAsset && BindingAsset && BindingAsset->TargetSkeletalMesh && BindingAsset->SourceSkeletalMesh)
	{
		// Use the LOD0 skeletal mesh to extract the vertices used for the RBF weight computation
		const int32 MeshLODIndex = 0;

		// Get the target mesh vertices (source and target)
		const FSkeletalMeshRenderData* SkeletalMeshData_Target = BindingAsset->TargetSkeletalMesh->GetResourceForRendering();
		TArray<FVector> MeshVertexPositionsBuffer_Target;
		ExtractSkeletalVertexPosition(SkeletalMeshData_Target, MeshLODIndex, MeshVertexPositionsBuffer_Target);

		// Apply RBF deformation to each group of guides and render strands
		const int32 NumGroups = BindingAsset->HairGroupDatas.Num();

		// Use the vertices positions from the HairDescription instead of the GroomAsset since the latter
		// may contain decimated or auto-generated guides depending on the import settings
		FProcessedHairDescription ProcessedHairDescription;
		FGroomBuilder::ProcessHairDescription(InGroomAsset->GetHairDescription(), ProcessedHairDescription);

		TArray<FRBFDeformedPositions> DeformedPositions;
		DeformedPositions.SetNum(NumGroups);
		// Sanity check to insure that the groom has all the original vertices
		for (int32 GroupIt = 0; GroupIt < NumGroups; ++GroupIt)
		{
			check(InGroomAsset->HairGroupsInterpolation[GroupIt].DecimationSettings.VertexDecimation == 1);
			check(InGroomAsset->HairGroupsInterpolation[GroupIt].DecimationSettings.CurveDecimation == 1);
		}

		// Note that the GroupID from the HairGroups cannot be used as the GroupIndex since 
		// the former may not be strictly increasing nor consecutive
		// but the ordering of the groups does represent the GroupIndex		
		int32 GroupIndex = 0;
		for (FProcessedHairDescription::FHairGroups::TConstIterator GroupIt = ProcessedHairDescription.HairGroups.CreateConstIterator(); GroupIt; ++GroupIt)
		{
			const FHairGroupData& OriginalHairGroupData = GroupIt->Value.Value;
			const FHairGroupData& HairGroupData = InGroomAsset->HairGroupsData[GroupIndex];

			// Get deformed guides
			// If the groom override the value, we output dummy value for the guides, since they won't be used
			if (InGroomAsset->HairGroupsInterpolation[GroupIndex].InterpolationSettings.bOverrideGuides)
			{
				const uint32 OriginalVertexCount = OriginalHairGroupData.Guides.Data.StrandsPoints.Num();
				DeformedPositions[GroupIndex].GuideStrands.Init(FVector::ZeroVector, OriginalVertexCount);
			}
			else
			{
				DeformedPositions[GroupIndex].GuideStrands = GetDeformedHairStrandsPositions(
					MeshVertexPositionsBuffer_Target,
					HairGroupData.Guides.Data,
					MeshLODIndex,
					SkeletalMeshData_Target,
					BindingAsset->HairGroupDatas[GroupIndex].SimRootData.VertexToCurveIndexBuffer,
					BindingAsset->HairGroupDatas[GroupIndex].SimRootData.MeshProjectionLODs[MeshLODIndex]);
			}

			// Get deformed render strands
			DeformedPositions[GroupIndex].RenderStrands = GetDeformedHairStrandsPositions(
				MeshVertexPositionsBuffer_Target,
				HairGroupData.Strands.Data,
				MeshLODIndex,
				SkeletalMeshData_Target,
				BindingAsset->HairGroupDatas[GroupIndex].RenRootData.VertexToCurveIndexBuffer,
				BindingAsset->HairGroupDatas[GroupIndex].RenRootData.MeshProjectionLODs[MeshLODIndex]);

			++GroupIndex;
		}

		// Apply changes onto cards and meshes (OutGroomASset already contain duplicated mesh asset
		for (FHairGroupsCardsSourceDescription& Desc : OutGroomAsset->HairGroupsCards)
		{
			UStaticMesh* Mesh = nullptr;
			if (Desc.SourceType == EHairCardsSourceType::Procedural)
			{
				Mesh = Desc.ProceduralMesh;
			}
			else if (Desc.SourceType == EHairCardsSourceType::Imported)
			{
				Mesh = Desc.ImportedMesh;
			}
			if (!Mesh)
			{
				continue;
			}

			if (Desc.GroupIndex >= 0)
			{
				Mesh->ConditionalPostLoad();
				DeformStaticMeshPositions(Mesh, MeshVertexPositionsBuffer_Target, BindingAsset->HairGroupDatas[Desc.GroupIndex].RenRootData.MeshProjectionLODs[MeshLODIndex]);
			}
		} 

		// Apply RBF deformation to mesh vertices
		for (FHairGroupsMeshesSourceDescription& Desc : OutGroomAsset->HairGroupsMeshes)
		{
			if (UStaticMesh* Mesh = Desc.ImportedMesh)
			{
				if (Mesh->GetNumLODs() == 0 || Desc.GroupIndex < 0 || Desc.GroupIndex >= InGroomAsset->GetNumHairGroups() || Desc.LODIndex == -1)
				{
					continue;
				}

				Mesh->ConditionalPostLoad();
				DeformStaticMeshPositions(Mesh, MeshVertexPositionsBuffer_Target, BindingAsset->HairGroupDatas[Desc.GroupIndex].RenRootData.MeshProjectionLODs[MeshLODIndex]);
			}
		}

		// Finally, the deformed guides and strands are applied to the GroomAsset
		ApplyDeformationToGroom(DeformedPositions, OutGroomAsset);
	}
#endif // #if WITH_EDITORONLY_DATA
}
