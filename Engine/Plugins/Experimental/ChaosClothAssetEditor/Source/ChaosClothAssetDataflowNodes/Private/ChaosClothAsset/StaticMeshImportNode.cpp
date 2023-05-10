// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/StaticMeshImportNode.h"
#include "ChaosClothAsset/DataflowNodes.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothCollection.h"
#include "Engine/StaticMesh.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "StaticMeshAttributes.h"
#include "Materials/MaterialInterface.h"
#include "StaticMeshOperations.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StaticMeshImportNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetStaticMeshImportNode"

namespace UE::Chaos::ClothAsset::Private
{
struct FBuildVertex
{
	FVertexID OrigVertID;
	FVertexInstanceID OrigVertInstanceID;

	FVector3f Position;
	TArray<FVector2f> UVs;
	FVector3f Normal;
	FVector3f Tangent;
	float BiNormalSign;
	FVector4f Color;

	bool operator==(const FBuildVertex& Other) const
	{
		if (OrigVertID != Other.OrigVertID)
		{
			return false;
		}

		// No need to check position since it's from OrigVertID

		if (!ensure(UVs.Num() == Other.UVs.Num()))
		{
			return false;
		}
		for (int32 UVChannelIndex = 0; UVChannelIndex < UVs.Num(); ++UVChannelIndex)
		{
			if (!UVs[UVChannelIndex].Equals(Other.UVs[UVChannelIndex], UE_THRESH_UVS_ARE_SAME))
			{
				return false;
			}
		}
		if (!Normal.Equals(Other.Normal, UE_THRESH_NORMALS_ARE_SAME))
		{
			return false;
		}
		if (!Tangent.Equals(Other.Tangent, UE_THRESH_NORMALS_ARE_SAME))
		{
			return false;
		}
		if (BiNormalSign != Other.BiNormalSign) // I think these are just -1 or 1, so just comparing them
		{
			return false;
		}
		// It looks like we just quantize the 0-1 color values to uint8s. (Call FLinearColor::ToFColor(bSRGB = false)) when we consume these.
		// Going to be a little more strict in case we ever decide to switch to the gamma conversion.
		if (!Color.Equals(Other.Color, UE_THRESH_NORMALS_ARE_SAME))
		{
			return false;
		}
		return true;
	}
};

static void MergeVertexInstances(const FMeshDescription* const MeshDescription, TArray<FBuildVertex>& MergedVertices, TArray<int32>& VertexInstanceToMerged)
{
	FStaticMeshConstAttributes Attributes(*MeshDescription);

	TVertexAttributesConstRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();

	TVertexInstanceAttributesConstRef<FVector2f> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
	TVertexInstanceAttributesConstRef<FVector3f> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesConstRef<FVector3f> VertexInstanceTangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesConstRef<float> VertexInstanceBiNormalSigns = Attributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesConstRef<FVector4f> VertexInstanceColors = Attributes.GetVertexInstanceColors();

	// Merge identical vertex instances that correspond with the same vertex.
	auto MakeBuildVertex = [&VertexPositions, &VertexInstanceUVs, &VertexInstanceNormals, &VertexInstanceTangents, &VertexInstanceBiNormalSigns, &VertexInstanceColors](FVertexID VertID, FVertexInstanceID VertexInstanceID)
	{
		FBuildVertex BuildVertex;
		BuildVertex.OrigVertID = VertID;
		BuildVertex.OrigVertInstanceID = VertexInstanceID;
		BuildVertex.Position = VertexPositions[VertID];
		BuildVertex.UVs.SetNumUninitialized(VertexInstanceUVs.GetNumChannels());
		for (int32 UVChannelIndex = 0; UVChannelIndex < VertexInstanceUVs.GetNumChannels(); ++UVChannelIndex)
		{
			BuildVertex.UVs[UVChannelIndex] = VertexInstanceUVs.Get(VertexInstanceID, UVChannelIndex);
		}
		BuildVertex.Normal = VertexInstanceNormals[VertexInstanceID];
		BuildVertex.Tangent = VertexInstanceTangents[VertexInstanceID];
		BuildVertex.BiNormalSign = VertexInstanceBiNormalSigns[VertexInstanceID];
		BuildVertex.Color = VertexInstanceColors[VertexInstanceID];
		return BuildVertex;
	};
	MergedVertices.Reset(MeshDescription->VertexInstances().GetArraySize());
	VertexInstanceToMerged.Init(INDEX_NONE, MeshDescription->VertexInstances().GetArraySize());

	for (const FVertexID& VertID : MeshDescription->Vertices().GetElementIDs())
	{
		TConstArrayView<FVertexInstanceID> VertexInstances = MeshDescription->GetVertexVertexInstanceIDs(VertID);
		if (VertexInstances.IsEmpty())
		{
			continue;
		}
		const int32 FirstMergedVert = MergedVertices.Add(MakeBuildVertex(VertID, VertexInstances[0]));
		VertexInstanceToMerged[VertexInstances[0].GetValue()] = FirstMergedVert;
		for (int32 LocalInstance = 1; LocalInstance < VertexInstances.Num(); ++LocalInstance)
		{
			FBuildVertex BuildVert = MakeBuildVertex(VertID, VertexInstances[LocalInstance]);
			bool bFoundDuplicate = false;
			for (int32 CompareIndex = FirstMergedVert; CompareIndex < MergedVertices.Num(); ++CompareIndex)
			{
				if (BuildVert == MergedVertices[CompareIndex])
				{
					bFoundDuplicate = true;
					VertexInstanceToMerged[VertexInstances[LocalInstance].GetValue()] = CompareIndex;
					break;
				}
			}

			if (!bFoundDuplicate)
			{
				const int32 AddedMergedVert = MergedVertices.Add(MoveTemp(BuildVert));
				VertexInstanceToMerged[VertexInstances[LocalInstance].GetValue()] = AddedMergedVert;
			}
		}
	}
}

static void InitializeRenderPatternDataFromMeshDescription(const FMeshDescription* const InMeshDescription, const FMeshBuildSettings& BuildSettings, FCollectionClothPatternFacade& RenderPattern)
{
	const FMeshDescription* MeshDescription = InMeshDescription;
	FMeshDescription WritableMeshDescription;
	if (BuildSettings.bRecomputeNormals || BuildSettings.bRecomputeTangents)
	{
		// check if any are invalid
		bool bHasInvalidNormals, bHasInvalidTangents;
		FStaticMeshOperations::AreNormalsAndTangentsValid(*InMeshDescription, bHasInvalidNormals, bHasInvalidTangents);

		// if neither are invalid we are not going to recompute
		if (bHasInvalidNormals || bHasInvalidTangents)
		{
			WritableMeshDescription = *InMeshDescription;
			MeshDescription = &WritableMeshDescription;
			FStaticMeshAttributes Attributes(WritableMeshDescription);
			if (!Attributes.GetTriangleNormals().IsValid() || !Attributes.GetTriangleTangents().IsValid())
			{
				// If these attributes don't exist, create them and compute their values for each triangle
				FStaticMeshOperations::ComputeTriangleTangentsAndNormals(WritableMeshDescription);
			}

			EComputeNTBsFlags ComputeNTBsOptions = EComputeNTBsFlags::BlendOverlappingNormals;
			ComputeNTBsOptions |= BuildSettings.bRecomputeNormals ? EComputeNTBsFlags::Normals : EComputeNTBsFlags::None;
			ComputeNTBsOptions |= BuildSettings.bRecomputeTangents ? EComputeNTBsFlags::Tangents : EComputeNTBsFlags::None;
			ComputeNTBsOptions |= BuildSettings.bUseMikkTSpace ? EComputeNTBsFlags::UseMikkTSpace : EComputeNTBsFlags::None;
			ComputeNTBsOptions |= BuildSettings.bComputeWeightedNormals ? EComputeNTBsFlags::WeightedNTBs : EComputeNTBsFlags::None;
			ComputeNTBsOptions |= BuildSettings.bRemoveDegenerates ? EComputeNTBsFlags::IgnoreDegenerateTriangles : EComputeNTBsFlags::None;

			FStaticMeshOperations::ComputeTangentsAndNormals(WritableMeshDescription, ComputeNTBsOptions);
		}
	}

	// Merge vertex instances that share the same vertex. These will become the pattern vertices.
	TArray<FBuildVertex> MergedVertices;
	TArray<int32> VertexInstanceToMerged;
	MergeVertexInstances(MeshDescription, MergedVertices, VertexInstanceToMerged);

	RenderPattern.SetNumRenderVertices(MergedVertices.Num());
	RenderPattern.SetNumRenderFaces(MeshDescription->Triangles().Num());

	// Vertex data (this will MoveTemp stuff out of MergedVertices!!)
	TArrayView<FVector3f> RenderPosition = RenderPattern.GetRenderPosition();
	TArrayView<FVector3f> RenderNormal = RenderPattern.GetRenderNormal();
	TArrayView<FVector3f> RenderTangentU = RenderPattern.GetRenderTangentU();
	TArrayView<FVector3f> RenderTangentV = RenderPattern.GetRenderTangentV();
	TArrayView<TArray<FVector2f>> RenderUVs = RenderPattern.GetRenderUVs();
	TArrayView<FLinearColor> RenderColor = RenderPattern.GetRenderColor();
	for (int32 VertexIndex = 0; VertexIndex < MergedVertices.Num(); ++VertexIndex)
	{
		FBuildVertex& BuildVertex = MergedVertices[VertexIndex];
		RenderPosition[VertexIndex] = BuildVertex.Position;
		RenderNormal[VertexIndex] = BuildVertex.Normal;
		RenderTangentU[VertexIndex] = BuildVertex.Tangent;
		RenderTangentV[VertexIndex] = FVector3f::CrossProduct(BuildVertex.Normal, BuildVertex.Tangent).GetSafeNormal() * BuildVertex.BiNormalSign;
		RenderUVs[VertexIndex] = MoveTemp(BuildVertex.UVs);
		RenderColor[VertexIndex] = FLinearColor(BuildVertex.Color);
	}

	// Face data
	TArrayView<FIntVector3> RenderIndices = RenderPattern.GetRenderIndices();
	TArrayView<int32> RenderMaterialIndex = RenderPattern.GetRenderMaterialIndex();
	int32 FaceIndex = 0;
	for (const FTriangleID TriangleID : MeshDescription->Triangles().GetElementIDs())
	{
		const FPolygonGroupID PolygonGroupID = MeshDescription->GetTrianglePolygonGroup(TriangleID);
		RenderMaterialIndex[FaceIndex] = PolygonGroupID.GetValue();

		const TConstArrayView<FVertexInstanceID> VertexInstances = MeshDescription->GetTriangleVertexInstances(TriangleID);
		check(VertexInstances.Num() == 3);
		check(MergedVertices.IsValidIndex(VertexInstanceToMerged[VertexInstances[0].GetValue()]));
		check(MergedVertices.IsValidIndex(VertexInstanceToMerged[VertexInstances[1].GetValue()]));
		check(MergedVertices.IsValidIndex(VertexInstanceToMerged[VertexInstances[2].GetValue()]));
		RenderIndices[FaceIndex] = FIntVector3(VertexInstanceToMerged[VertexInstances[0].GetValue()],
			VertexInstanceToMerged[VertexInstances[1].GetValue()], VertexInstanceToMerged[VertexInstances[2].GetValue()]);
		++FaceIndex;
	}
}

} // namespace UE::Chaos::ClothAsset::Private

FChaosClothAssetStaticMeshImportNode::FChaosClothAssetStaticMeshImportNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterOutputConnection(&Collection);
}

void FChaosClothAssetStaticMeshImportNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	using namespace UE::Chaos::ClothAsset;
	using namespace UE::Chaos::ClothAsset::DataflowNodes;

	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		if (StaticMesh && (bImportAsSimMesh || bImportAsRenderMesh))
		{
			// Evaluate in collection
			const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>();
			FCollectionClothFacade ClothFacade(ClothCollection);
			ClothFacade.DefineSchema();

			const int32 NumLods = StaticMesh->GetNumSourceModels();
			for (int32 LodIndex = 0; LodIndex < NumLods; ++LodIndex)
			{
				const FMeshDescription* const MeshDescription = StaticMesh->GetMeshDescription(LodIndex);
				check(MeshDescription);

				FCollectionClothLodFacade ClothLodFacade = ClothFacade.AddGetLod();
				ClothLodFacade.Reset();

				if (bImportAsSimMesh)
				{
					FMeshDescriptionToDynamicMesh Converter;
					Converter.bPrintDebugMessages = false;
					Converter.bEnableOutputGroups = false;
					Converter.bVIDsFromNonManifoldMeshDescriptionAttr = true;
					UE::Geometry::FDynamicMesh3 DynamicMesh;

					Converter.Convert(MeshDescription, DynamicMesh);
					ClothLodFacade.Initialize(DynamicMesh, UVChannel, UVScale, bUseUVIslands);
				}
				if (bImportAsRenderMesh)
				{
					// Add render data into a single pattern for now
					FCollectionClothPatternFacade RenderPattern = ClothLodFacade.AddGetPattern();
					UE::Chaos::ClothAsset::Private::InitializeRenderPatternDataFromMeshDescription(MeshDescription, StaticMesh->GetSourceModel(LodIndex).BuildSettings, RenderPattern);

					// Set material path names
					const TArray<FStaticMaterial>& StaticMaterials = StaticMesh->GetStaticMaterials();
					ClothLodFacade.SetNumMaterials(StaticMaterials.Num());
					TArrayView<FString> RenderMaterialPathNames = ClothLodFacade.GetRenderMaterialPathName();
					for(int32 MaterialIndex = 0; MaterialIndex < StaticMaterials.Num(); ++MaterialIndex)
					{
						if (ensure(StaticMaterials[MaterialIndex].MaterialInterface))
						{
							RenderMaterialPathNames[MaterialIndex] = StaticMaterials[MaterialIndex].MaterialInterface->GetPathName();
						}
					}
				}

				// Set a default skeleton
				static const TCHAR* const DefaultSkeletonPathName = TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh_Skeleton.DefaultSkeletalMesh_Skeleton");
				ClothLodFacade.SetSkeletonAssetPathName(DefaultSkeletonPathName);
			}

			// Make sure that whatever happens there is always at least one empty LOD to avoid crashing the render data
			if (ClothFacade.GetNumLods() < 1)
			{
				ClothFacade.AddLod();
			}
			SetValue<FManagedArrayCollection>(Context, *ClothCollection, &Collection);
		}
	}
}

#undef LOCTEXT_NAMESPACE
