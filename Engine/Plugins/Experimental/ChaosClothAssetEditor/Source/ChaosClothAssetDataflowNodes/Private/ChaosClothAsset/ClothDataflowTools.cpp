// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothDataflowTools.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/ClothCollection.h"
#include "BoneWeights.h"
#include "Rendering/SkeletalMeshLODModel.h"

namespace UE::Chaos::ClothAsset
{
void FClothDataflowTools::AddRenderPatternFromSkeletalMeshSection(const TSharedPtr<FManagedArrayCollection>& ClothCollection, const FSkeletalMeshLODModel& SkeletalMeshModel, const int32 SectionIndex, const FString& RenderMaterialPathName)
{
	check(SectionIndex < SkeletalMeshModel.Sections.Num());
	check(ClothCollection.IsValid());

	FCollectionClothFacade Cloth(ClothCollection);
	FCollectionClothRenderPatternFacade ClothPatternFacade = Cloth.AddGetRenderPattern();

	const FSkelMeshSection& Section = SkeletalMeshModel.Sections[SectionIndex];
	ClothPatternFacade.SetNumRenderVertices(Section.NumVertices);
	ClothPatternFacade.SetNumRenderFaces(Section.NumTriangles);

	TArrayView<FVector3f> RenderPosition = ClothPatternFacade.GetRenderPosition();
	TArrayView<FVector3f> RenderNormal = ClothPatternFacade.GetRenderNormal();
	TArrayView<FVector3f> RenderTangentU = ClothPatternFacade.GetRenderTangentU();
	TArrayView<FVector3f> RenderTangentV = ClothPatternFacade.GetRenderTangentV();
	TArrayView<TArray<FVector2f>> RenderUVs = ClothPatternFacade.GetRenderUVs();
	TArrayView<FLinearColor> RenderColor = ClothPatternFacade.GetRenderColor();
	TArrayView<TArray<int32>> RenderBoneIndices = ClothPatternFacade.GetRenderBoneIndices();
	TArrayView<TArray<float>> RenderBoneWeights = ClothPatternFacade.GetRenderBoneWeights();
	for (int32 VertexIndex = 0; VertexIndex < Section.NumVertices; ++VertexIndex)
	{
		const FSoftSkinVertex& SoftVertex = Section.SoftVertices[VertexIndex];

		RenderPosition[VertexIndex] = SoftVertex.Position;
		RenderNormal[VertexIndex] = SoftVertex.TangentZ;
		RenderTangentU[VertexIndex] = SoftVertex.TangentX;
		RenderTangentV[VertexIndex] = SoftVertex.TangentY;
		RenderUVs[VertexIndex].SetNum(MAX_TEXCOORDS);
		for (int32 TexCoordIndex = 0; TexCoordIndex < MAX_TEXCOORDS; ++TexCoordIndex)
		{
			RenderUVs[VertexIndex][TexCoordIndex] = SoftVertex.UVs[TexCoordIndex];
		}

		RenderColor[VertexIndex] = FLinearColor(SoftVertex.Color);

		const int32 NumBones = Section.MaxBoneInfluences;
		RenderBoneIndices[VertexIndex].SetNum(NumBones);
		RenderBoneWeights[VertexIndex].SetNum(NumBones);
		for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
		{
			RenderBoneIndices[VertexIndex][BoneIndex] = (int32)Section.BoneMap[(int32)SoftVertex.InfluenceBones[BoneIndex]];
			RenderBoneWeights[VertexIndex][BoneIndex] = (float)SoftVertex.InfluenceWeights[BoneIndex] * UE::AnimationCore::InvMaxRawBoneWeightFloat;
		}
	}

	const int32 VertexOffset = ClothPatternFacade.GetRenderVerticesOffset();
	TArrayView<FIntVector3> RenderIndices = ClothPatternFacade.GetRenderIndices();
	for (uint32 FaceIndex = 0; FaceIndex < Section.NumTriangles; ++FaceIndex)
	{
		const uint32 IndexOffset = Section.BaseIndex + FaceIndex * 3;
		RenderIndices[FaceIndex] = FIntVector3(
			SkeletalMeshModel.IndexBuffer[IndexOffset + 0] - Section.BaseVertexIndex + VertexOffset,
			SkeletalMeshModel.IndexBuffer[IndexOffset + 1] - Section.BaseVertexIndex + VertexOffset,
			SkeletalMeshModel.IndexBuffer[IndexOffset + 2] - Section.BaseVertexIndex + VertexOffset
		);
	}
	ClothPatternFacade.SetRenderMaterialPathName(RenderMaterialPathName);
}
}  // End namespace UE::Chaos::ClothAsset
