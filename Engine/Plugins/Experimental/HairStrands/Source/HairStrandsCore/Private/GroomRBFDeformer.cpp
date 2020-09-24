// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomRBFDeformer.h"
#include "Async/ParallelFor.h"
#include "GroomAsset.h"
#include "GroomBindingAsset.h"
#include "GroomBuilder.h"
#include "Rendering/SkeletalMeshRenderData.h"

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
	OutSampleDeformationsBuffer.SetNum(MaxSampleCount + 4);
	for (uint32 SampleIndex = 0; SampleIndex < MaxSampleCount + 4; ++SampleIndex)
	{
		uint32 WeightsOffset = SampleIndex * (MaxSampleCount + 4);
		FVector SampleDeformation(FVector::ZeroVector);
		for (uint32 i = 0; i < MaxSampleCount; ++i, ++WeightsOffset)
		{
			SampleDeformation += InterpolationWeightsBuffer[WeightsOffset] *
				(SampleDeformedPositionsBuffer[i] - SampleRestPositionsBuffer[i]);
		}

		OutSampleDeformationsBuffer[SampleIndex] = SampleDeformation;
	}
}

// HairStrandsGuideDeform.usf
FVector DisplacePosition(
	const FVector& Pos,
	const FVector& SimRestOffset,
	const FVector& SimDeformedOffset,
	uint32 SampleCount,
	const TArray<FVector4>& RestSamplePositionsBuffer,
	const TArray<FVector>& MeshSampleWeightsBuffer
)
{
	const FVector RestControlPoint = Pos + SimRestOffset;
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
	return ControlPoint - SimDeformedOffset;
}

void DeformStrands(
	const FVector& SimRestOffset,
	const FVector& SimDeformedOffset,
	uint32 VertexCount,
	uint32 SampleCount,
	const TArray<FVector>& SimRestPosePositionBuffer,
	const TArray<FVector4>& RestSamplePositionsBuffer,
	const TArray<FVector>& MeshSampleWeightsBuffer,
	TArray<FVector>& OutSimDeformedPositionBuffer
)
{
	OutSimDeformedPositionBuffer.SetNum(SimRestPosePositionBuffer.Num());
	ParallelFor(VertexCount, [&](uint32 VertexIndex)
	{
		const FVector& ControlPoint = SimRestPosePositionBuffer[VertexIndex];
		const FVector DisplacedPosition = DisplacePosition(ControlPoint, SimRestOffset, SimDeformedOffset, SampleCount, RestSamplePositionsBuffer, MeshSampleWeightsBuffer);
		OutSimDeformedPositionBuffer[VertexIndex] = DisplacedPosition;
	});
}

TArray<FVector> GetDeformedHairStrandsPositions(
	const FHairStrandsRootData::FMeshProjectionLOD& RestLODData,
	const TArray<FVector>& VertexPositionsBuffer,
	const FHairStrandsDatas& HairStrandsData,
	const FVector& DeformationOffset
)
{
	// Init the mesh samples with the target mesh vertices
	const int32 MaxVertexCount = VertexPositionsBuffer.Num();
	const uint32 MaxSampleCount = RestLODData.SampleCount;
	const TArray<uint32>& SampleIndicesBuffer = RestLODData.MeshSampleIndicesBuffer;
	TArray<FVector> OutSamplePositionsBuffer;

	InitMeshSamples(MaxVertexCount, VertexPositionsBuffer, MaxSampleCount, SampleIndicesBuffer, OutSamplePositionsBuffer);

	// Update those vertices with the RBF interpolation weights
	const TArray<float>& InterpolationWeightsBuffer = RestLODData.MeshInterpolationWeightsBuffer;
	const TArray<FVector4>& SampleRestPositionsBuffer = RestLODData.RestSamplePositionsBuffer;
	const TArray<FVector>& SampleDeformedPositionsBuffer = OutSamplePositionsBuffer;
	TArray<FVector> OutSampleDeformationsBuffer;

	UpdateMeshSamples(MaxSampleCount, InterpolationWeightsBuffer, SampleRestPositionsBuffer, SampleDeformedPositionsBuffer, OutSampleDeformationsBuffer);

	// Get the strands vertices positions centered at their bounding box
	const FHairStrandsPoints& Points = HairStrandsData.StrandsPoints;
	const uint32 VertexCount = Points.Num();
	const FVector RestOffset(FVector::ZeroVector); // HairStrandsData.BoundingBox.GetCenter();

	TArray<FVector> OutPositions;
	OutPositions.SetNum(VertexCount);
	for (uint32 Index = 0; Index < VertexCount; ++Index)
	{
		const FVector& PointPosition = Points.PointsPosition[Index];
		OutPositions[Index] = PointPosition - RestOffset;
	}

	// Deform the strands vertices with the deformed mesh samples
	const TArray<FVector>& SimRestPosePositionBuffer = OutPositions;
	const TArray<FVector4>& RestSamplePositionsBuffer = RestLODData.RestSamplePositionsBuffer;
	const TArray<FVector>& MeshSampleWeightsBuffer = OutSampleDeformationsBuffer;
	TArray<FVector> OutDeformedPositionBuffer;

	DeformStrands(RestOffset, DeformationOffset, VertexCount, MaxSampleCount, SimRestPosePositionBuffer, RestSamplePositionsBuffer, MeshSampleWeightsBuffer, OutDeformedPositionBuffer);

	// Put back the strands in their initial space
	for (int32 Index = 0; Index < OutDeformedPositionBuffer.Num(); ++Index)
	{
		OutDeformedPositionBuffer[Index] += RestOffset;
	}

	return MoveTemp(OutDeformedPositionBuffer);
}

struct FRBFDeformedPositions 
{
	TArray<FVector> RenderStrands;
	TArray<FVector> GuideStrands;
};

#if WITH_EDITORONLY_DATA
void ApplyDeformationToGroom(const TArray<FRBFDeformedPositions>& DeformedPositions, UGroomAsset* GroomAsset)
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

	GroomAsset->CommitHairDescription(MoveTemp(HairDescription));
	GroomAsset->UpdateHairGroupsInfo();
	GroomAsset->InitResource();
}
#endif //#if WITH_EDITORONLY_DATA

UGroomAsset* FGroomRBFDeformer::GetRBFDeformedGroomAsset(const UGroomAsset* InGroomAsset, const UGroomBindingAsset* BindingAsset, const FVector& DeformationOffset)
{
#if WITH_EDITORONLY_DATA
	if (InGroomAsset && BindingAsset && BindingAsset->TargetSkeletalMesh)
	{
		UGroomAsset* GroomAsset = DuplicateObject<UGroomAsset>(InGroomAsset, nullptr);

		const int32 LODIndex = 0;

		// Get the target mesh vertices
		FSkeletalMeshRenderData* SkeletalMeshData = BindingAsset->TargetSkeletalMesh->GetResourceForRendering();
		const uint32 TargetVertexCount = SkeletalMeshData->LODRenderData[LODIndex].StaticVertexBuffers.PositionVertexBuffer.GetNumVertices();
		
		TArray<FVector> VertexPositionsBuffer;
		VertexPositionsBuffer.SetNum(TargetVertexCount);
		for (uint32 VertexIndex = 0; VertexIndex < TargetVertexCount; ++VertexIndex)
		{
			VertexPositionsBuffer[VertexIndex] = SkeletalMeshData->LODRenderData[LODIndex].StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex);
		}

		// Apply RBF deformation to each group of guides and render strands
		const int32 NumGroups = BindingAsset->HairGroupDatas.Num();

		// Use the vertices positions from the HairDescription instead of the GroomAsset since the latter
		// may contain decimated or auto-generated guides depending on the import settings
		FProcessedHairDescription ProcessedHairDescription;
		FGroomBuilder::ProcessHairDescription(InGroomAsset->GetHairDescription(), ProcessedHairDescription);

		TArray<FRBFDeformedPositions> DeformedPositions;
		DeformedPositions.SetNum(NumGroups);

		// Note that the GroupID from the HairGroups cannot be used as the GroupIndex since 
		// the former may not be strictly increasing nor consecutive
		// but the ordering of the groups does represent the GroupIndex
		int32 GroupIndex = 0;
		for (FProcessedHairDescription::FHairGroups::TConstIterator GroupIt = ProcessedHairDescription.HairGroups.CreateConstIterator(); GroupIt; ++GroupIt)
		{
			const FHairGroupData& HairGroupData = GroupIt->Value.Value;

			// Get deformed guides
			DeformedPositions[GroupIndex].GuideStrands = GetDeformedHairStrandsPositions(
				BindingAsset->HairGroupDatas[GroupIndex].SimRootData.MeshProjectionLODs[LODIndex],
				VertexPositionsBuffer,
				HairGroupData.Guides.Data,
				DeformationOffset
			);

			// Get deformed render strands
			DeformedPositions[GroupIndex].RenderStrands = GetDeformedHairStrandsPositions(
				BindingAsset->HairGroupDatas[GroupIndex].RenRootData.MeshProjectionLODs[LODIndex],
				VertexPositionsBuffer,
				HairGroupData.Strands.Data,
				DeformationOffset
			);

			++GroupIndex;
		}

		// Finally, the deformed guides and strands are applied to the GroomAsset
		ApplyDeformationToGroom(DeformedPositions, GroomAsset);

		return GroomAsset;
	}
#endif // #if WITH_EDITORONLY_DATA
	return nullptr;
}
