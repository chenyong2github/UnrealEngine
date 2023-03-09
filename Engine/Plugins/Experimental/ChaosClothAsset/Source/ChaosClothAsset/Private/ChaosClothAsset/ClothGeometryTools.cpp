// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothGeometryTools.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "Math/Vector.h"

namespace UE::Chaos::ClothAsset
{
	// TODO: Move these functions to the cloth facade?
	bool FClothGeometryTools::HasSimMesh(const TSharedPtr<const FManagedArrayCollection>& ClothCollection)
	{
		bool bHasSimMesh = false;

		FCollectionClothConstFacade ClothFacade(ClothCollection);

		for (int32 LodIndex = 0; LodIndex < ClothFacade.GetNumLods(); ++LodIndex)
		{
			FCollectionClothLodConstFacade ClothLodFacade = ClothFacade.GetLod(LodIndex);

			for (int32 PatternIndex = ClothLodFacade.GetNumPatterns() - 1; PatternIndex >= 0 ; --PatternIndex)  // Use a reverse order to avoid having to move previous elements
			{
				FCollectionClothPatternConstFacade ClothPatternFacade = ClothLodFacade.GetPattern(PatternIndex);
				if (ClothPatternFacade.GetNumSimVertices() && ClothPatternFacade.GetNumSimFaces())
				{
					bHasSimMesh = true;
					break;
				}
			}
		}
		return bHasSimMesh;
	}

	bool FClothGeometryTools::HasRenderMesh(const TSharedPtr<const FManagedArrayCollection>& ClothCollection)
	{
		bool bHasRenderMesh = false;

		FCollectionClothConstFacade ClothFacade(ClothCollection);

		for (int32 LodIndex = 0; LodIndex < ClothFacade.GetNumLods(); ++LodIndex)
		{
			FCollectionClothLodConstFacade ClothLodFacade = ClothFacade.GetLod(LodIndex);

			for (int32 PatternIndex = ClothLodFacade.GetNumPatterns() - 1; PatternIndex >= 0 ; --PatternIndex)  // Use a reverse order to avoid having to move previous elements
			{
				FCollectionClothPatternConstFacade ClothPatternFacade = ClothLodFacade.GetPattern(PatternIndex);
				if (ClothPatternFacade.GetNumRenderVertices() && ClothPatternFacade.GetNumRenderFaces())
				{
					bHasRenderMesh = true;
					break;
				}
			}
		}
		return bHasRenderMesh;
	}

	void FClothGeometryTools::SetSkeletonAssetPathName(const TSharedPtr<FManagedArrayCollection>& ClothCollection, const FString& SkeletonAssetPathName)
	{
		FCollectionClothFacade ClothFacade(ClothCollection);

		for (int32 LodIndex = 0; LodIndex < ClothFacade.GetNumLods(); ++LodIndex)
		{
			FCollectionClothLodFacade ClothLodFacade = ClothFacade.GetLod(LodIndex);
			ClothLodFacade.SetSkeletonAssetPathName(SkeletonAssetPathName);
		}
	}

	void FClothGeometryTools::SetPhysicsAssetPathName(const TSharedPtr<FManagedArrayCollection>& ClothCollection, const FString& PhysicsAssetPathName)
	{
		FCollectionClothFacade ClothFacade(ClothCollection);

		for (int32 LodIndex = 0; LodIndex < ClothFacade.GetNumLods(); ++LodIndex)
		{
			FCollectionClothLodFacade ClothLodFacade = ClothFacade.GetLod(LodIndex);
			ClothLodFacade.SetPhysicsAssetPathName(PhysicsAssetPathName);
		}
	}

	void FClothGeometryTools::DeleteRenderMesh(const TSharedPtr<FManagedArrayCollection>& ClothCollection)
	{
		FCollectionClothFacade ClothFacade(ClothCollection);

		for (int32 LodIndex = 0; LodIndex < ClothFacade.GetNumLods(); ++LodIndex)
		{
			FCollectionClothLodFacade ClothLodFacade = ClothFacade.GetLod(LodIndex);

			for (int32 PatternIndex = ClothLodFacade.GetNumPatterns() - 1; PatternIndex >= 0 ; --PatternIndex)  // Use a reverse order to avoid having to move previous elements
			{
				FCollectionClothPatternFacade ClothPatternFacade = ClothLodFacade.GetPattern(PatternIndex);
				ClothPatternFacade.SetNumRenderVertices(0);
				ClothPatternFacade.SetNumRenderFaces(0);
			}

			ClothLodFacade.SetNumMaterials(0);
		}
	}

	void FClothGeometryTools::CopySimMeshToRenderMesh(const TSharedPtr<FManagedArrayCollection>& ClothCollection, const FString& RenderMaterialPathName)
	{
		FCollectionClothFacade ClothFacade(ClothCollection);

		for (int32 LodIndex = 0; LodIndex < ClothFacade.GetNumLods(); ++LodIndex)
		{
			FCollectionClothLodFacade ClothLodFacade = ClothFacade.GetLod(LodIndex);

			// Add the material if it doesn't already exists in this LOD
			const TConstArrayView<FString> RenderMaterialPathNameArray = ClothLodFacade.GetRenderMaterialPathName();
			int32 MaterialIndex = RenderMaterialPathNameArray.Find(RenderMaterialPathName);
			if (MaterialIndex == INDEX_NONE)
			{
				MaterialIndex = RenderMaterialPathNameArray.Num();
				ClothLodFacade.SetNumMaterials(MaterialIndex + 1);
				ClothLodFacade.GetRenderMaterialPathName()[MaterialIndex] = RenderMaterialPathName;
			}

			// Copy sim data to render data
			const TConstArrayView<FVector2f> LodSimPosition = ClothLodFacade.GetSimPosition();
			const TConstArrayView<FVector3f> LodSimRestPosition = ClothLodFacade.GetSimRestPosition();

			for (int32 PatternIndex = 0; PatternIndex < ClothLodFacade.GetNumPatterns(); ++PatternIndex)
			{
				FCollectionClothPatternFacade ClothPatternFacade = ClothLodFacade.GetPattern(PatternIndex);

				const int32 NumVertices = ClothPatternFacade.GetNumSimVertices();
				const int32 NumFaces = ClothPatternFacade.GetNumSimFaces();

				if (!NumVertices || !NumFaces)
				{
					ClothPatternFacade.SetNumRenderVertices(0);
					ClothPatternFacade.SetNumRenderFaces(0);
					continue;
				}

				ClothPatternFacade.SetNumRenderVertices(NumVertices);
				ClothPatternFacade.SetNumRenderFaces(NumFaces);

				const TConstArrayView<FVector2f> SimPosition = ClothPatternFacade.GetSimPosition();
				const TConstArrayView<FVector3f> SimRestPosition = ClothPatternFacade.GetSimRestPosition();
				const TConstArrayView<FVector3f> SimRestNormal = ClothPatternFacade.GetSimRestNormal();
				const TArrayView<FVector3f> RenderTangentU = ClothPatternFacade.GetRenderTangentU();
				const TArrayView<FVector3f> RenderTangentV = ClothPatternFacade.GetRenderTangentV();

				FVector2f MinPosition(TNumericLimits<float>::Max());
				FVector2f MaxPosition(TNumericLimits<float>::Lowest());

				// Calculate UVs scale and zero out tangents
				for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
				{
					MinPosition = FVector2f::Min(MinPosition, SimPosition[VertexIndex]);
					MaxPosition = FVector2f::Max(MaxPosition, SimPosition[VertexIndex]);

					RenderTangentU[VertexIndex] = FVector3f::ZeroVector;
					RenderTangentV[VertexIndex] = FVector3f::ZeroVector;
				}
				const FVector2f UVScale = MaxPosition - MinPosition;
				const FVector2f UVInvScale(
					UVScale.X < SMALL_NUMBER ? 0.f : 1.f / UVScale.X,
					UVScale.Y < SMALL_NUMBER ? 0.f : 1.f / UVScale.Y);

				// Face group
				const TConstArrayView<FIntVector3> SimIndices = ClothPatternFacade.GetSimIndices();
				const TArrayView<FIntVector3> RenderIndices = ClothPatternFacade.GetRenderIndices();
				const TArrayView<int32> RenderMaterialIndex = ClothPatternFacade.GetRenderMaterialIndex();
				const TArrayView<FVector3f> LodRenderTangentU = ClothLodFacade.GetRenderTangentU();
				const TArrayView<FVector3f> LodRenderTangentV = ClothLodFacade.GetRenderTangentV();
				for (int32 FaceIndex = 0; FaceIndex < NumFaces; ++FaceIndex)
				{
					const FIntVector3& Face = SimIndices[FaceIndex];

					RenderIndices[FaceIndex] = Face;
					RenderMaterialIndex[FaceIndex] = MaterialIndex;

					const FVector3f Pos01 = LodSimRestPosition[Face[1]] - LodSimRestPosition[Face[0]];
					const FVector3f Pos02 = LodSimRestPosition[Face[2]] - LodSimRestPosition[Face[0]];
					const FVector2f UV01 = LodSimPosition[Face[1]] - LodSimPosition[Face[0]];
					const FVector2f UV02 = LodSimPosition[Face[2]] - LodSimPosition[Face[0]];
					
					const float Denom = UV01.X * UV02.Y - UV01.Y * UV02.X;
					const float InvDenom = (Denom < SMALL_NUMBER) ? 0.f : 1.f / Denom;
					const FVector3f TangentU = (Pos01 * UV02.Y - Pos02 * UV01.Y) * InvDenom;
					const FVector3f TangentV = (Pos02 * UV01.X - Pos01 * UV02.X) * InvDenom;

					for (int32 PointIndex = 0; PointIndex < 3; ++PointIndex)
					{
						LodRenderTangentU[Face[PointIndex]] += TangentU;
						LodRenderTangentV[Face[PointIndex]] += TangentV;
					}
				}

				// Vertex group
				const TArrayView<FVector3f> RenderPosition = ClothPatternFacade.GetRenderPosition();
				const TArrayView<FVector3f> RenderNormal = ClothPatternFacade.GetRenderNormal();
				const TArrayView<TArray<FVector2f>> RenderUVs = ClothPatternFacade.GetRenderUVs();
				const TArrayView<FLinearColor> RenderColor = ClothPatternFacade.GetRenderColor();
				const TArrayView<int32> RenderNumBoneInfluences = ClothPatternFacade.GetRenderNumBoneInfluences();
				const TArrayView<TArray<int32>> RenderBoneIndices = ClothPatternFacade.GetRenderBoneIndices();
				const TArrayView<TArray<float>> RenderBoneWeights = ClothPatternFacade.GetRenderBoneWeights();
				const TArrayView<int32> SimNumBoneInfluences = ClothPatternFacade.GetSimNumBoneInfluences();
				const TArrayView<TArray<int32>> SimBoneIndices = ClothPatternFacade.GetSimBoneIndices();
				const TArrayView<TArray<float>> SimBoneWeights = ClothPatternFacade.GetSimBoneWeights();

				for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
				{
					RenderPosition[VertexIndex] = SimRestPosition[VertexIndex];
					RenderNormal[VertexIndex] = -SimRestNormal[VertexIndex];  // Simulation normals use reverse normals
					RenderUVs[VertexIndex] = { SimPosition[VertexIndex] * UVInvScale };
					RenderColor[VertexIndex] = FLinearColor::White;
					RenderTangentU[VertexIndex].Normalize();
					RenderTangentV[VertexIndex].Normalize();
					RenderNumBoneInfluences[VertexIndex] = SimNumBoneInfluences[VertexIndex];
					RenderBoneIndices[VertexIndex] = SimBoneIndices[VertexIndex];
					RenderBoneWeights[VertexIndex] = SimBoneWeights[VertexIndex];
				}
			}
		}
	}

	void FClothGeometryTools::ReverseNormals(
		const TSharedPtr<FManagedArrayCollection>& ClothCollection,
		bool bReverseSimMeshNormals,
		bool bReverseRenderMeshNormals,
		const TArray<int32>& PatternSelection)
	{
		auto ReverseSimNormals = [](const TArrayView<FVector3f>& SimRestNormal)
			{
				for (int32 VertexIndex = 0; VertexIndex < SimRestNormal.Num(); ++VertexIndex)
				{
					SimRestNormal[VertexIndex] = -SimRestNormal[VertexIndex];
				}
			};
		auto ReverseRenderNormals = [](const TArrayView<FVector3f>& RenderNormal, const TArrayView<FVector3f>& RenderTangentU)
		{
			check(RenderNormal.Num() == RenderTangentU.Num())
			for (int32 VertexIndex = 0; VertexIndex < RenderNormal.Num(); ++VertexIndex)
			{
				RenderNormal[VertexIndex] = -RenderNormal[VertexIndex];      // Equivalent of rotating the normal basis
				RenderTangentU[VertexIndex] = -RenderTangentU[VertexIndex];  // around tangent V
			}
		};

		FCollectionClothFacade ClothFacade(ClothCollection);

		for (int32 LodIndex = 0; LodIndex < ClothFacade.GetNumLods(); ++LodIndex)
		{
			FCollectionClothLodFacade ClothLodFacade = ClothFacade.GetLod(LodIndex);

			if (PatternSelection.IsEmpty())
			{
				if (bReverseSimMeshNormals)
				{
					ReverseSimNormals(ClothLodFacade.GetSimRestNormal());
				}
				if (bReverseRenderMeshNormals)
				{
					ReverseRenderNormals(ClothLodFacade.GetRenderNormal(), ClothLodFacade.GetRenderTangentU());
				}
			}
			else
			{
				for (int32 PatternIndex = 0; PatternIndex < ClothLodFacade.GetNumPatterns(); ++PatternIndex)
				{
					if (PatternSelection.Find(PatternIndex) != INDEX_NONE)
					{
						FCollectionClothPatternFacade ClothPatternFacade = ClothLodFacade.GetPattern(PatternIndex);

						if (bReverseSimMeshNormals)
						{
							ReverseSimNormals(ClothPatternFacade.GetSimRestNormal());
						}
						if (bReverseRenderMeshNormals)
						{
							ReverseRenderNormals(ClothPatternFacade.GetRenderNormal(), ClothPatternFacade.GetRenderTangentU());
						}
					}
				}
			}
		}
	}

	void FClothGeometryTools::BindMeshToRootBone(
		const TSharedPtr<FManagedArrayCollection>& ClothCollection,
		bool bBindSimMesh,
		bool bBindRenderMesh,
		const TArray<int32> Lods)
	{
		if (!bBindSimMesh && !bBindRenderMesh)
		{
			return;
		}
		
		FCollectionClothFacade ClothFacade(ClothCollection);

		TArray<int32> LodsToBind;
		if (Lods.IsEmpty())
		{
			LodsToBind.Reserve(ClothFacade.GetNumLods());
			
			for (int32 LodIndex = 0; LodIndex < ClothFacade.GetNumLods(); ++LodIndex)
			{
				LodsToBind.Add(LodIndex);
			}
		}
		else
		{	
			LodsToBind.Reserve(Lods.Num());
			
			for (const int32 Lod : Lods)
			{	
				// Make sure the Lod indices are valid.
				if (Lod < ClothFacade.GetNumLods())
				{
					LodsToBind.Add(Lod);
				}
			}
		}

		for (const int32 LodIndex : LodsToBind)
		{
			FCollectionClothLodFacade ClothLodFacade = ClothFacade.GetLod(LodIndex);
			
			if (bBindSimMesh)
			{	
				const int32 NumVertices = ClothLodFacade.GetNumSimVertices();
				TArrayView<int32> NumBoneInfluences = ClothLodFacade.GetSimNumBoneInfluences();
				TArrayView<TArray<int32>> BoneIndices = ClothLodFacade.GetSimBoneIndices();
				TArrayView<TArray<float>> BoneWeights = ClothLodFacade.GetSimBoneWeights();

				for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
				{
					NumBoneInfluences[VertexIndex] = 1;
					BoneIndices[VertexIndex] = {0};
					BoneWeights[VertexIndex] = {1.0f};
				}
			}

			if (bBindRenderMesh)
			{
				const int32 NumVertices = ClothLodFacade.GetNumRenderVertices();
				TArrayView<int32> NumBoneInfluences = ClothLodFacade.GetRenderNumBoneInfluences();
				TArrayView<TArray<int32>> BoneIndices = ClothLodFacade.GetRenderBoneIndices();
				TArrayView<TArray<float>> BoneWeights = ClothLodFacade.GetRenderBoneWeights();

				for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
				{
					NumBoneInfluences[VertexIndex] = 1;
					BoneIndices[VertexIndex] = {0};
					BoneWeights[VertexIndex] = {1.0f};
				}
			}
		}
	}
}  // End namespace UE::Chaos::ClothAsset
