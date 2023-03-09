// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothAssetBuilderEditor.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Modules/ModuleManager.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Utils/ClothingMeshUtils.h"
#include "BoneWeights.h"
#include "MeshUtilities.h"
#include "PointWeightMap.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ClothAssetBuilderEditor)

void UClothAssetBuilderEditor::BuildLod(FSkeletalMeshLODModel& LODModel, const UChaosClothAsset& ClothAsset, int32 LodIndex) const
{
	using namespace UE::Chaos::ClothAsset;

	// Start from an empty LODModel
	LODModel.Empty();

	// Clear the mesh infos, none are stored on this asset
	LODModel.ImportedMeshInfos.Empty();
	LODModel.MaxImportVertex = 0;

	// Set 1 texture coordinate
	LODModel.NumTexCoords = 1;

	// Init the size of the vertex buffer
	LODModel.NumVertices = 0;

	// Create a table to remap the LOD materials to the asset materials
	const TArray<FSkeletalMaterial>& Materials = ClothAsset.GetMaterials();

	const TSharedPtr<const FManagedArrayCollection> ClothCollection = ClothAsset.GetClothCollection();

	const FCollectionClothConstFacade ClothFacade(ClothCollection);
	check(LodIndex < ClothFacade.GetNumLods())
	const FCollectionClothLodConstFacade ClothLodFacade = ClothFacade.GetLod(LodIndex);

	const int32 NumLodMaterials = ClothLodFacade.GetNumMaterials();
	const TConstArrayView<FString> LodRenderMaterialPathName = ClothLodFacade.GetRenderMaterialPathName();

	TArray<int32> LodMaterialMap;
	LodMaterialMap.AddUninitialized(NumLodMaterials);

	for (int32 LodMaterialIndex = 0; LodMaterialIndex < NumLodMaterials; ++LodMaterialIndex)
	{
		const FString& RenderMaterialPathName = LodRenderMaterialPathName[LodMaterialIndex];
		const int32 SkeletalMaterialIndex = Materials.IndexOfByPredicate([&RenderMaterialPathName](const FSkeletalMaterial& SkeletalMaterial)
			{
				return SkeletalMaterial.MaterialInterface && SkeletalMaterial.MaterialInterface->GetPathName() == RenderMaterialPathName;
			});
		LodMaterialMap[LodMaterialIndex] = SkeletalMaterialIndex;
	}

	// Build the section/faces map from the LOD patterns
	TMap<int32, TArray<int32>> SectionFacesMap;
	SectionFacesMap.Reserve(Materials.Num());

	TArray<uint32> LodRenderIndexRemap;

	const int32 NumRenderFaces = ClothLodFacade.GetNumRenderFaces();
	const TConstArrayView<int32> LodRenderMaterialIndex = ClothLodFacade.GetRenderMaterialIndex();

	for (int32 RenderFaceIndex = 0; RenderFaceIndex < NumRenderFaces; ++RenderFaceIndex)
	{
		const int32 RenderMaterialIndex = LodMaterialMap[LodRenderMaterialIndex[RenderFaceIndex]];

		TArray<int32>& SectionFaces = SectionFacesMap.FindOrAdd(RenderMaterialIndex);
		SectionFaces.Add(RenderFaceIndex);
	}

	const int32 NumRenderVertices = ClothLodFacade.GetNumRenderVertices();
	LodRenderIndexRemap.SetNumUninitialized(NumRenderVertices);

	// Keep track of the active bone indices for this LOD model
	TSet<FBoneIndexType> ActiveBoneIndices;
	ActiveBoneIndices.Reserve(ClothAsset.RefSkeleton.GetNum());

	// Load the mesh utilities module used to optimized the index buffer
	IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");

	// Build the sim mesh descriptor for creation of the sections' mesh to mesh mapping data
	const ClothingMeshUtils::ClothMeshDesc SourceMesh(
		GetSimPositions(ClothAsset, LodIndex),
		GetSimIndices(ClothAsset, LodIndex));  // Let it calculate the averaged normals as to match the simulation data output

	const int32 NumLodSimVertices = GetNumVertices(ClothAsset, LodIndex);

	// Retrieve the MaxDistance map
	FPointWeightMap MaxDistances;
	MaxDistances.Initialize(NumLodSimVertices);
	for (int32 Index = 0; Index < NumLodSimVertices; ++Index)
	{
		MaxDistances[Index] = 200.f;
	}

	// Populate this LOD's sections and the LOD index buffer
	const int32 NumSections = SectionFacesMap.Num();
	LODModel.Sections.SetNum(NumSections);

	int32 SectionIndex = 0;
	int32 BaseIndex = 0;
	for (const TPair<int32, TArray<int32>>& SectionFaces : SectionFacesMap)
	{
		FSkelMeshSection& Section = LODModel.Sections[SectionIndex];

		Section.OriginalDataSectionIndex = SectionIndex++;

		const int32 MaterialIndex = SectionFaces.Key;
		const TArray<int32>& Faces = SectionFaces.Value;
		const int32 NumFaces = Faces.Num();
		const int32 NumIndices = NumFaces * 3;

		// Build the section face data (indices)
		Section.MaterialIndex = (uint16)MaterialIndex;

		Section.BaseIndex = (uint32)BaseIndex;
		BaseIndex += NumIndices;

		Section.NumTriangles = (uint32)NumFaces;

		TArray<uint32> Indices;
		TSet<int32> UniqueIndicesSet;
		Indices.SetNumUninitialized(NumIndices);

		const TConstArrayView<FIntVector3> LodRenderIndices = ClothLodFacade.GetRenderIndices();

		for (int32 FaceIndex = 0; FaceIndex < NumFaces; ++FaceIndex)
		{
			const FIntVector3& RenderIndices = LodRenderIndices[Faces[FaceIndex]];
			for (int32 VertexIndex = 0; VertexIndex < 3; ++VertexIndex)
			{
				const int32 RenderIndex = RenderIndices[VertexIndex];
				Indices[FaceIndex * 3 + VertexIndex] = (uint32)RenderIndex;
				UniqueIndicesSet.Add(RenderIndex);
			}
		}

		MeshUtilities.CacheOptimizeIndexBuffer(Indices);

		LODModel.IndexBuffer.Append(MoveTemp(Indices));

		// Build the section vertex data from the unique indices
		TArray<int32> UniqueIndices = UniqueIndicesSet.Array();
		const int32 NumVertices = UniqueIndices.Num();

		Section.SoftVertices.SetNumUninitialized(NumVertices);
		Section.NumVertices = NumVertices;
		Section.BaseVertexIndex = LODModel.NumVertices;
		LODModel.NumVertices += (uint32)NumVertices;

		// Map reference skeleton bone index to the index in the section's bone map
		TMap<FBoneIndexType, FBoneIndexType> ReferenceToSectionBoneMap; 

		// Track how many bones we added to the section's bone map so far
		int CurSectionBoneMapNum = 0; 

		const TConstArrayView<FVector3f> LodRenderPosition = ClothLodFacade.GetRenderPosition();
		const TConstArrayView<FVector3f> LodRenderTangentU = ClothLodFacade.GetRenderTangentU();
		const TConstArrayView<FVector3f> LodRenderTangentV = ClothLodFacade.GetRenderTangentV();
		const TConstArrayView<FVector3f> LodRenderNormal = ClothLodFacade.GetRenderNormal();
		const TConstArrayView<FLinearColor> LodRenderColor = ClothLodFacade.GetRenderColor();
		const TConstArrayView<TArray<FVector2f>> LodRenderUVs = ClothLodFacade.GetRenderUVs();
		const TConstArrayView<int32> LodRenderNumBoneInfluences = ClothLodFacade.GetRenderNumBoneInfluences();
		const TConstArrayView<TArray<int32>> LodRenderBoneIndices = ClothLodFacade.GetRenderBoneIndices();
		const TConstArrayView<TArray<float>> LodRenderBoneWeights = ClothLodFacade.GetRenderBoneWeights();

		for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
		{
			const int32 RenderIndex = UniqueIndices[VertexIndex];
			LODModel.MaxImportVertex = FMath::Max(LODModel.MaxImportVertex, RenderIndex);

			LodRenderIndexRemap[RenderIndex] = Section.BaseVertexIndex + (uint32)VertexIndex;

			FSoftSkinVertex& SoftVertex = Section.SoftVertices[VertexIndex];

			SoftVertex.Position = LodRenderPosition[RenderIndex];
			SoftVertex.TangentX = LodRenderTangentU[RenderIndex];
			SoftVertex.TangentY = LodRenderTangentV[RenderIndex];
			SoftVertex.TangentZ = LodRenderNormal[RenderIndex];

			constexpr bool bSRGB = false; // Avoid linear to srgb conversion
			SoftVertex.Color = LodRenderColor[RenderIndex].ToFColor(bSRGB);

			const TArray<FVector2f>& RenderUVs = LodRenderUVs[RenderIndex];
			for (int32 TexCoord = 0; TexCoord < FMath::Min(RenderUVs.Num(), (int32)MAX_TEXCOORDS); ++TexCoord)
			{
				SoftVertex.UVs[TexCoord] = RenderUVs[TexCoord];
			}

			const int32 NumInfluences = LodRenderNumBoneInfluences[RenderIndex];
			
			// Add all of the bones that have non-zero influence to the section's bone map and keep track of the order
			// that we added the reference bone via CurSectionBoneMapNum
			for (int32 Influence = 0; Influence < NumInfluences; ++Influence)
			{
				const FBoneIndexType InfluenceBone = (FBoneIndexType)LodRenderBoneIndices[RenderIndex][Influence];

				if (ReferenceToSectionBoneMap.Contains(InfluenceBone) == false)
				{
					ReferenceToSectionBoneMap.Add(InfluenceBone, CurSectionBoneMapNum);
					++CurSectionBoneMapNum; 
				}
			}

			int32 Influence = 0;
			for (;Influence < NumInfluences; ++Influence)
			{
				const FBoneIndexType InfluenceBone = (FBoneIndexType)LodRenderBoneIndices[RenderIndex][Influence];
				const float InWeight = LodRenderBoneWeights[RenderIndex][Influence];
				const uint16 InfluenceWeight = static_cast<uint16>(InWeight * static_cast<float>(UE::AnimationCore::MaxRawBoneWeight) + 0.5f);
				
				// FSoftSkinVertex::InfluenceBones contain indices into the section's bone map and not the reference
				// skeleton, so we need to remap
				const FBoneIndexType* const MappedIndexPtr = ReferenceToSectionBoneMap.Find(InfluenceBone);
				
				// ReferenceToSectionBoneMap should always contain InfluenceBone since it was added above
				checkSlow(MappedIndexPtr);
				if (MappedIndexPtr != nullptr)
				{
					SoftVertex.InfluenceBones[Influence] = *MappedIndexPtr;
					SoftVertex.InfluenceWeights[Influence] = InfluenceWeight;
				}
			}
			
			for (;Influence < MAX_TOTAL_INFLUENCES; ++Influence)
			{
				SoftVertex.InfluenceBones[Influence] = 0;
				SoftVertex.InfluenceWeights[Influence] = 0;
			}
		}

		// Initialize the section bone map
		Section.BoneMap.SetNumUninitialized(ReferenceToSectionBoneMap.Num());
		for (const TPair<FBoneIndexType, FBoneIndexType>& Pair : ReferenceToSectionBoneMap)
		{
			Section.BoneMap[Pair.Value] = Pair.Key;
		}

		// Remap the LOD indices with the new vertex indices
		for (uint32& RenderIndex : LODModel.IndexBuffer)
		{
			RenderIndex = LodRenderIndexRemap[RenderIndex];
		}

		ActiveBoneIndices.Append(Section.BoneMap);

		// Update max bone influences
		Section.CalcMaxBoneInfluences();
		Section.CalcUse16BitBoneIndex();

		// Setup clothing data
		Section.ClothMappingDataLODs.SetNum(1);  // TODO: LODBias maps for raytracing

		Section.ClothingData.AssetLodIndex = LodIndex;
		Section.ClothingData.AssetGuid = ClothAsset.AssetGuid;  // There is only one cloth asset,
		Section.CorrespondClothAssetIndex = 0;       // this one

		TArray<FVector3f> RenderPositions;
		TArray<FVector3f> RenderNormals;
		TArray<FVector3f> RenderTangents;
		for (const FSoftSkinVertex& SoftVert : Section.SoftVertices)
		{
			RenderPositions.Add(SoftVert.Position);
			RenderNormals.Add(SoftVert.TangentZ);
			RenderTangents.Add(SoftVert.TangentX);
		}

		const ClothingMeshUtils::ClothMeshDesc TargetMesh(RenderPositions, 
			RenderNormals, 
			RenderTangents,
			LODModel.IndexBuffer);

		ClothingMeshUtils::GenerateMeshToMeshVertData(
			Section.ClothMappingDataLODs[0],
			TargetMesh,
			SourceMesh,
			&MaxDistances,
			ClothAsset.bSmoothTransition,
			ClothAsset.bUseMultipleInfluences,
			ClothAsset.SkinningKernelRadius);

		// Save the original indices for the newly added vertices
		LODModel.MeshToImportVertexMap.Append(MoveTemp(UniqueIndices));

		// Compute the overlapping vertices map (inspired from MeshUtilities::BuildSkeletalMesh)
		const TArray<FSoftSkinVertex>& SoftVertices = Section.SoftVertices;

		typedef TPair<float, int32> FIndexAndZ;  // Acceleration structure, list of vertex Z / index pairs
		TArray<FIndexAndZ> IndexAndZs;
		IndexAndZs.Reserve(NumVertices);
		for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
		{
			const FVector3f& Position = SoftVertices[VertexIndex].Position;

			const float Z = 0.30f * Position.X + 0.33f * Position.Y + 0.37f * Position.Z;
			IndexAndZs.Emplace(Z, VertexIndex);
		}
		IndexAndZs.Sort([](const FIndexAndZ& A, const FIndexAndZ& B) { return A.Key < B.Key; });

		for (int32 Index0 = 0; Index0 < IndexAndZs.Num(); ++Index0)
		{
			const float Z0 = IndexAndZs[Index0].Key;
			const uint32 VertexIndex0 = IndexAndZs[Index0].Value;
			const FVector3f& Position0 = SoftVertices[VertexIndex0].Position;

			// Only need to search forward, since we add pairs both ways
			for (int32 Index1 = Index0 + 1; Index1 < IndexAndZs.Num() && FMath::Abs(IndexAndZs[Index1].Key - Z0) <= THRESH_POINTS_ARE_SAME; ++Index1)
			{
				const uint32 VertexIndex1 = IndexAndZs[Index1].Value;
				const FVector3f& Position1 = SoftVertices[VertexIndex1].Position;

				if (PointsEqual(Position0, Position1))
				{
					// Add to the overlapping map
					TArray<int32>& SrcValueArray = Section.OverlappingVertices.FindOrAdd(VertexIndex0);
					SrcValueArray.Add(VertexIndex1);

					TArray<int32>& IterValueArray = Section.OverlappingVertices.FindOrAdd(VertexIndex1);
					IterValueArray.Add(VertexIndex0);
				}
			}
		}

		// Copy to user section data, otherwise the section data set above would get lost when the user section gets synced
		FSkelMeshSourceSectionUserData::GetSourceSectionUserData(LODModel.UserSectionsData, Section);
	}


	// Update the active bone indices on the LOD model
	LODModel.ActiveBoneIndices = ActiveBoneIndices.Array();

	// Ensure parent exists with incoming active bone indices, and the result should be sorted
	ClothAsset.RefSkeleton.EnsureParentsExistAndSort(LODModel.ActiveBoneIndices);

	// Compute the required bones for this model.
	USkeletalMesh::CalculateRequiredBones(LODModel, ClothAsset.RefSkeleton, nullptr);
}
