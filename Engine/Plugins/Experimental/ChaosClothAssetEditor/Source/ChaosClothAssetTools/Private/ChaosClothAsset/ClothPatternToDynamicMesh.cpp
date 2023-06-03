// Copyright Epic Games, Inc. All Rights Reserved. 

#if WITH_EDITOR

#include "ChaosClothAsset/ClothPatternToDynamicMesh.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothCollection.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ToDynamicMesh.h"
#include "DynamicMesh/MeshNormals.h"
#include "SkeletalMeshAttributes.h"
#include "Animation/Skeleton.h"

namespace UE::Chaos::ClothAsset
{

//
// Wrapper for accessing a Cloth Pattern. Implements the interface expected by TToDynamicMesh<>.
//
class FClothPatternWrapper
{
public:

	typedef int32 TriIDType;
	typedef int32 VertIDType;
	typedef int32 WedgeIDType;

	typedef int32 UVIDType;
	typedef int32 NormalIDType;
	typedef int32 ColorIDType;

	FClothPatternWrapper(const FCollectionClothConstFacade& ClothFacade, int32 PatternIndex, EClothPatternVertexType VertexDataType) :
		VertexDataType(VertexDataType),
		Cloth(ClothFacade)
	{
		int32 NumFaces = 0;
		int32 FaceOffset = 0;
		if (PatternIndex == INDEX_NONE)
		{
			// All patterns in one dynamic mesh
			switch (VertexDataType)
			{
			case EClothPatternVertexType::Render:
			{
				const int32 NumVertices = Cloth.GetNumRenderVertices();
				VertIDs.SetNum(NumVertices);
				for (int32 VtxIndex = 0; VtxIndex < NumVertices; ++VtxIndex)
				{
					VertIDs[VtxIndex] = VtxIndex;
				}
				NormalIDs = VertIDs;

				NumFaces = Cloth.GetNumRenderFaces();
				FaceOffset = 0;
			} break;
			case EClothPatternVertexType::Sim2D:
			{
				const int32 NumVertices = Cloth.GetNumSimVertices2D();
				VertIDs.SetNum(NumVertices);
				for (int32 VtxIndex = 0; VtxIndex < NumVertices; ++VtxIndex)
				{
					VertIDs[VtxIndex] = VtxIndex;
				}
				NormalIDs = Cloth.GetSimVertex3DLookup();

				NumFaces = Cloth.GetNumSimFaces();
				FaceOffset = 0;
			}
			break;
			case EClothPatternVertexType::Sim3D:
			{
				const int32 NumVertices = Cloth.GetNumSimVertices3D();
				VertIDs.SetNum(NumVertices);
				for (int32 VtxIndex = 0; VtxIndex < NumVertices; ++VtxIndex)
				{
					VertIDs[VtxIndex] = VtxIndex;
				}
				NormalIDs = VertIDs;

				NumFaces = Cloth.GetNumSimFaces();
				FaceOffset = 0;
			}break;
			default:
				checkNoEntry();
			}
		}
		else
		{
			switch (VertexDataType)
			{
			case EClothPatternVertexType::Render:
			{
				FCollectionClothRenderPatternConstFacade Pattern = Cloth.GetRenderPattern(PatternIndex);
				const int32 NumVertices = Pattern.GetNumRenderVertices();
				const int32 VertexOffset = Pattern.GetRenderVerticesOffset();
				VertIDs.SetNum(NumVertices);
				for (int32 VtxIndex = 0; VtxIndex < NumVertices; ++VtxIndex)
				{
					VertIDs[VtxIndex] = VtxIndex + VertexOffset;
				}
				NormalIDs = VertIDs;

				NumFaces = Pattern.GetNumRenderFaces();
				FaceOffset = Pattern.GetRenderFacesOffset();
			} break;
			case EClothPatternVertexType::Sim2D:
			{
				FCollectionClothSimPatternConstFacade Pattern = Cloth.GetSimPattern(PatternIndex);
				const int32 NumVertices = Pattern.GetNumSimVertices2D();
				const int32 VertexOffset = Pattern.GetSimVertices2DOffset();
				VertIDs.SetNum(NumVertices);
				for (int32 VtxIndex = 0; VtxIndex < NumVertices; ++VtxIndex)
				{
					VertIDs[VtxIndex] = VtxIndex + VertexOffset;
				}
				NormalIDs = Pattern.GetSimVertex3DLookup();

				NumFaces = Pattern.GetNumSimFaces();
				FaceOffset = Pattern.GetSimFacesOffset();
			} break;
			case EClothPatternVertexType::Sim3D:
			{
				FCollectionClothSimPatternConstFacade Pattern = Cloth.GetSimPattern(PatternIndex);
				VertIDs = Pattern.GetSimVertex3DLookup();
				NormalIDs = Pattern.GetSimVertex3DLookup();

				NumFaces = Pattern.GetNumSimFaces();
				FaceOffset = Pattern.GetSimFacesOffset();
			} break;
			default:
				checkNoEntry();
			}
		}

		TriIDs.SetNum(NumFaces);
		for (int32 TriIndex = 0; TriIndex < NumFaces; ++TriIndex)
		{
			TriIDs[TriIndex] = TriIndex + FaceOffset;;
		}

		if(VertexDataType != EClothPatternVertexType::Render)
		{
			//
			// Weight map layers precomputation
			//
			// No weight maps for render mesh
			WeightMapNames = Cloth.GetWeightMapNames();
		}

		// Set the reference skeleton if available
		const FString& SkeletonPathName = Cloth.GetSkeletonAssetPathName();
		const USkeleton* const Skeleton = !SkeletonPathName.IsEmpty() ?
			LoadObject<USkeleton>(nullptr, *SkeletonPathName, nullptr, LOAD_None, nullptr) :
			nullptr;
		RefSkeleton = Skeleton ? &Skeleton->GetReferenceSkeleton() : nullptr;
		ensureMsgf(RefSkeleton, TEXT("Reference skeleton is not set"));
	}

	int32 NumTris() const
	{
		return TriIDs.Num();
	}

	int32 NumVerts() const
	{
		return VertIDs.Num();
	}

	int32 NumUVLayers() const
	{
		return (VertexDataType == EClothPatternVertexType::Render) ? 1 : 0;		// No UVs for Sim mesh
	}

	int32 NumWeightMapLayers() const
	{
		return WeightMapNames.Num();
	}

	FName GetWeightMapName(int32 LayerIndex) const
	{
		return WeightMapNames[LayerIndex];
	}

	float GetVertexWeight(int32 LayerIndex, VertIDType VertexIndex) const
	{
		// All weights live on 3D indices.
		const int32 VertexWeightIndex = (VertexDataType == EClothPatternVertexType::Sim2D) ? Cloth.GetSimVertex3DLookup()[VertexIndex] : VertexIndex;
		return Cloth.GetWeightMap(WeightMapNames[LayerIndex])[VertexWeightIndex];
	}

	// --"Vertex Buffer" info
	const TArray<VertIDType>& GetVertIDs() const
	{
		return VertIDs;
	}

	FVector3d GetPosition(VertIDType VtxID) const
	{
		switch (VertexDataType)
		{
			case EClothPatternVertexType::Render:
			{
				const TConstArrayView<FVector3f> RenderPositions = Cloth.GetRenderPosition();
				return FVector3d(RenderPositions[VtxID]);
			}
			case EClothPatternVertexType::Sim2D:
			{
				const TConstArrayView<FVector2f> SimPositions = Cloth.GetSimPosition2D();
				const FVector2f& Pos = SimPositions[VtxID];
				return FVector3d(Pos[0], Pos[1], 0.0);
			}
			case EClothPatternVertexType::Sim3D:
			{
				const TConstArrayView<FVector3f> SimPositions = Cloth.GetSimPosition3D();
				const FVector3f& Pos = SimPositions[VtxID];
				return FVector3d(Pos[0], Pos[1], Pos[2]);
			}
			default:
				checkNoEntry();
				return FVector3d();
		};
	}

	// --"Index Buffer" info
	const TArray<TriIDType>& GetTriIDs() const
	{
		return TriIDs;
	}

	bool GetTri(TriIDType TriID, VertIDType& VID0, VertIDType& VID1, VertIDType& VID2) const
	{
		FIntVector Face;

		switch (VertexDataType)
		{
		case EClothPatternVertexType::Render:
			Face = Cloth.GetRenderIndices()[TriID];
			break;
		case EClothPatternVertexType::Sim2D:
			Face = Cloth.GetSimIndices2D()[TriID];
			break;
		case EClothPatternVertexType::Sim3D:
			Face = Cloth.GetSimIndices3D()[TriID];
			break;
		default:
			checkNoEntry();
		}

		VID0 = Face[0];
		VID1 = Face[1];
		VID2 = Face[2];

		return true;
	}
	
	bool HasNormals() const
	{
		return true;
	}

	bool HasTangents() const
	{
		return VertexDataType == EClothPatternVertexType::Render;
	}

	bool HasBiTangents() const
	{
		return false;
	}

	bool HasColors() const
	{
		return VertexDataType == EClothPatternVertexType::Render;
	}

	// -- Access to per-wedge attributes -- //
	void GetWedgeIDs(const TriIDType& TriID, WedgeIDType& WID0, WedgeIDType& WID1, WedgeIDType& WID2) const
	{
		checkf(false, TEXT("FClothPatternWrapper: ClothPatterns are not expected to use Wedges"));
	}

	FVector2f GetWedgeUV(int32 UVLayerIndex, WedgeIDType WID) const
	{
		checkf(false, TEXT("FClothPatternWrapper: ClothPatterns are not expected to use Wedges"));
		return FVector2f();
	}

	FVector3f GetWedgeNormal(WedgeIDType WID) const
	{
		checkf(false, TEXT("FClothPatternWrapper: ClothPatterns are not expected to use Wedges"));
		return FVector3f();
	}

	FVector3f GetWedgeTangent(WedgeIDType WID) const
	{
		checkf(false, TEXT("FClothPatternWrapper: ClothPatterns are not expected to use Wedges"));
		return FVector3f();
	}

	FVector3f GetWedgeBiTangent(WedgeIDType WID) const
	{
		checkf(false, TEXT("FClothPatternWrapper: ClothPatterns are not expected to use Wedges"));
		return FVector3f();
	}
	
	FVector4f GetWedgeColor(WedgeIDType WID) const
	{
		checkf(false, TEXT("FClothPatternWrapper: ClothPatterns are not expected to use Wedges"));
		return FVector4f();
	}
	// -- End of per-wedge attribute access -- //

	int32 GetMaterialIndex(TriIDType TriID) const
	{
		checkf(false, TEXT("FClothPatternWrapper: Material indexing should be accomplished by passing a function into Convert"));
		return 0;
	}

	int32 NumSkinWeightAttributes() const 
	{ 
		return 1;
	}

	UE::AnimationCore::FBoneWeights GetVertexSkinWeight(int32 SkinWeightAttributeIndex, VertIDType InVertexID) const 
	{
		using namespace UE::AnimationCore;
		
		checkfSlow(SkinWeightAttributeIndex == 0, TEXT("Cloth assets should only have one skin weight profile")); 

		const bool bGetRenderMeshData = (VertexDataType == EClothPatternVertexType::Render);
		const TConstArrayView<TArray<int32>> BoneIndices = bGetRenderMeshData ? Cloth.GetRenderBoneIndices() : Cloth.GetSimBoneIndices();
		const TConstArrayView<TArray<float>> BoneWeights = bGetRenderMeshData ? Cloth.GetRenderBoneWeights() : Cloth.GetSimBoneWeights();

		// All weights live on 3D indices. Need to convert 2D index to 3D index.
		const VertIDType VertexID = (VertexDataType == EClothPatternVertexType::Sim2D) ? Cloth.GetSimVertex3DLookup()[InVertexID] : InVertexID;

		if (ensure(VertexID >= 0 && VertexID < BoneIndices.Num()))
		{
			const TArray<int32> Indices = BoneIndices[VertexID];
			const TArray<float> Weights = BoneWeights[VertexID];
			const int32 NumInfluences = Indices.Num();
			check(Weights.Num() == NumInfluences);
			
			TArray<FBoneWeight> BoneWeightArray;
			BoneWeightArray.SetNumUninitialized(NumInfluences);

			for (int32 Idx = 0; Idx < NumInfluences; ++Idx)
			{
				BoneWeightArray[Idx] = FBoneWeight(static_cast<FBoneIndexType>(Indices[Idx]), Weights[Idx]);
			}

			return FBoneWeights::Create(BoneWeightArray, FBoneWeightsSettings());
		}
		else 
		{
			return FBoneWeights();
		}
	}

	FName GetSkinWeightAttributeName(int32 SkinWeightAttributeIndex) const 
	{ 
		checkfSlow(SkinWeightAttributeIndex == 0, TEXT("Cloth assets should only have one skin weight profile")); 
		
		return FSkeletalMeshAttributes::DefaultSkinWeightProfileName;
	}

	int32 GetNumBones() const 
	{ 
		return RefSkeleton ? RefSkeleton->GetRawBoneNum() : 0;
	}

    FName GetBoneName(int32 BoneIdx) const
	{
		if (ensure(BoneIdx >= 0 && BoneIdx < GetNumBones()) && RefSkeleton)
		{
			return RefSkeleton->GetRawRefBoneInfo()[BoneIdx].Name;
		}
		
		return NAME_None;
	}

	int32 GetBoneParentIndex(int32 BoneIdx) const
	{
		if (ensure(BoneIdx >= 0 && BoneIdx < GetNumBones()) && RefSkeleton)
		{
			return RefSkeleton->GetRawRefBoneInfo()[BoneIdx].ParentIndex;
		}

		return INDEX_NONE;
	}

	FTransform GetBonePose(int32 BoneIdx) const
	{
		if (ensure(BoneIdx >= 0 && BoneIdx < GetNumBones()) && RefSkeleton)
		{
			return RefSkeleton->GetRawRefBonePose()[BoneIdx];
		}

		return FTransform::Identity;
	}

	FVector4f GetBoneColor(int32 BoneIdx) const
	{
		return FVector4f::One();
	}

	const TArray<int32>& GetNormalIDs() const 
	{ 
		return NormalIDs; 
	}

	FVector3f GetNormal(NormalIDType ID) const 
	{ 
		return (VertexDataType == EClothPatternVertexType::Render) ? Cloth.GetRenderNormal()[ID] : Cloth.GetSimNormal()[ID];
	}

	bool GetNormalTri(const TriIDType& TriID, NormalIDType& ID0, NormalIDType& ID1, NormalIDType& ID2) const 
	{ 
		if (VertexDataType == EClothPatternVertexType::Sim2D)
		{
			// All normal data lives on 3D indices.
			const FIntVector Face = Cloth.GetSimIndices3D()[TriID];

			ID0 = Face[0];
			ID1 = Face[1];
			ID2 = Face[2];

			return true;
		}
		else
		{
			return GetTri(TriID, ID0, ID1, ID2);
		}
	}
	
	const TArray<int32>& GetUVIDs(int32 LayerID) const 
	{ 
		return (VertexDataType == EClothPatternVertexType::Render) ? NormalIDs : EmptyArray;
	}

	FVector2f GetUV(int32 LayerID, UVIDType UVID) const 
	{ 
		checkf(VertexDataType == EClothPatternVertexType::Render, TEXT("Requested UVs from a Sim mesh"));
		const TConstArrayView<FVector2f> VertexUVs = Cloth.GetRenderUVs()[UVID];
		return VertexUVs[LayerID];
	}

	bool GetUVTri(int32 LayerID, const TriIDType& TriID, UVIDType& ID0, UVIDType& ID1, UVIDType& ID2) const 
	{ 
		return GetTri(TriID, ID0, ID1, ID2); 
	}
	
	const TArray<int32>& GetTangentIDs() const 
	{ 
		return (VertexDataType == EClothPatternVertexType::Render) ? NormalIDs : EmptyArray;
	}

	FVector3f GetTangent(NormalIDType ID) const 
	{ 
		checkf(VertexDataType == EClothPatternVertexType::Render, TEXT("Requested Tangent from a Sim mesh"));
		return Cloth.GetRenderTangentU()[ID];
	}

	bool GetTangentTri(const TriIDType& TriID, NormalIDType& ID0, NormalIDType& ID1, NormalIDType& ID2) const 
	{ 
		return GetNormalTri(TriID, ID0, ID1, ID2);
	}

	const TArray<int32>& GetBiTangentIDs() const 
	{ 
		return (VertexDataType == EClothPatternVertexType::Render) ? NormalIDs : EmptyArray;
	}

	FVector3f GetBiTangent(NormalIDType ID) const 
	{
		checkf(VertexDataType == EClothPatternVertexType::Render, TEXT("Requested Bitangent from a Sim mesh"));
		return Cloth.GetRenderTangentV()[ID];
	}

	bool GetBiTangentTri(const TriIDType& TriID, NormalIDType& ID0, NormalIDType& ID1, NormalIDType& ID2) const
	{
		return GetNormalTri(TriID, ID0, ID1, ID2);
	}

	const TArray<int32>& GetColorIDs() const 
	{ 
		return (VertexDataType == EClothPatternVertexType::Render) ? NormalIDs : EmptyArray;
	}

	FVector4f GetColor(ColorIDType VID) const 
	{
		checkf(VertexDataType == EClothPatternVertexType::Render, TEXT("Requested color from a Sim mesh"));
		return Cloth.GetRenderColor()[VID];
	}

	bool GetColorTri(const TriIDType& TriID, ColorIDType& ID0, ColorIDType& ID1, ColorIDType& ID2) const 
	{
		return GetNormalTri(TriID, ID0, ID1, ID2);
	}

private:

	const EClothPatternVertexType VertexDataType;
	const FCollectionClothConstFacade& Cloth;

	TArray<TriIDType> TriIDs;		// indices into Indices
	TArray<VertIDType> VertIDs;		// indices into Positions2D or Positions3D
	TArray<NormalIDType> NormalIDs; // indices into Normals (and all per vertex data other than positions)
	
	TArray<FName> WeightMapNames;

	TArray<int32> EmptyArray;

	const FReferenceSkeleton* RefSkeleton = nullptr;
};


void FClothPatternToDynamicMesh::Convert(const TSharedPtr<const FManagedArrayCollection> ClothCollection, int32 PatternIndex, EClothPatternVertexType VertexDataType, UE::Geometry::FDynamicMesh3& MeshOut)
{
	const FCollectionClothConstFacade ClothFacade(ClothCollection);

	// Actual conversion
	UE::Geometry::TToDynamicMesh<FClothPatternWrapper> PatternToDynamicMesh; 
	FClothPatternWrapper PatternWrapper(ClothFacade, PatternIndex, VertexDataType);

	const bool bDisableAttributes = false;
	auto TriangleToGroupFunction = [](FClothPatternWrapper::TriIDType) { return 0; };

	if (bDisableAttributes)
	{
		MeshOut.DiscardAttributes();
		PatternToDynamicMesh.ConvertWOAttributes(MeshOut, PatternWrapper, TriangleToGroupFunction);
	}
	else
	{
		MeshOut.EnableAttributes();

		const bool bIsRenderType = VertexDataType == EClothPatternVertexType::Render;
		auto TriangleToMaterialFunction = [PatternIndex, bIsRenderType, &ClothFacade](FClothPatternWrapper::TriIDType TriID)->int32
		{
			if (bIsRenderType)
			{
				if (PatternIndex != INDEX_NONE)
				{
					return PatternIndex;
				}
				const int32 FoundPattern = ClothFacade.FindRenderPatternByFaceIndex(TriID);
				if (ensure(FoundPattern != INDEX_NONE))
				{
					return FoundPattern;
				}
			}
			return INDEX_NONE;
		};

		constexpr bool bCopyTangents = false;
		PatternToDynamicMesh.Convert(MeshOut, PatternWrapper, TriangleToGroupFunction, TriangleToMaterialFunction, bCopyTangents);
	}

}

void FClothPatternToDynamicMesh::Convert(const UChaosClothAsset* ClothAssetMeshIn, int32 LODIndex, int32 PatternIndex, EClothPatternVertexType VertexDataType, UE::Geometry::FDynamicMesh3& MeshOut)
{
	const TArray<TSharedPtr<const FManagedArrayCollection>>& ClothCollections = ClothAssetMeshIn->GetClothCollections();
	check(ClothCollections.IsValidIndex(LODIndex));
	check(ClothCollections[LODIndex].IsValid());

	Convert(ClothCollections[LODIndex], PatternIndex, VertexDataType, MeshOut);
}

}	// namespace UE::Chaos::ClothAsset

#else

namespace UE::Chaos::ClothAsset
{

void FClothAssetToDynamicMesh::Convert(const UChaosClothAsset* ClothAssetMeshIn, FDynamicMesh3& MeshOut, int32 LODIndex, int32 PatternIndex)
{
	// Conversion only supported with editor.
	check(0);
}

}	// namespace UE::Chaos::ClothAsset

#endif  // end with editor

