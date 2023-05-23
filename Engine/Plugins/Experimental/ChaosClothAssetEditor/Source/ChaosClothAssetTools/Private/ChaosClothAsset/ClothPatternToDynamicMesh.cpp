// Copyright Epic Games, Inc. All Rights Reserved. 

#if WITH_EDITOR

#include "ChaosClothAsset/ClothPatternToDynamicMesh.h"
#include "ChaosClothAsset/ClothAsset.h"
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

	FClothPatternWrapper(const FCollectionClothConstFacade& ClothFacade, int32 LodIndex, int32 PatternIndex, EClothPatternVertexType VertexDataType) :
		VertexDataType(VertexDataType),
		Pattern(ClothFacade.GetLod(LodIndex).GetPattern(PatternIndex))
	{
		const int32 NumVertices = (VertexDataType == EClothPatternVertexType::Render) ? Pattern.GetNumRenderVertices() : Pattern.GetNumSimVertices();
		VertIDs.SetNum(NumVertices);
		for (int32 VtxIndex = 0; VtxIndex < NumVertices; ++VtxIndex)
		{
			VertIDs[VtxIndex] = VtxIndex;
		}

		const int32 NumFaces = (VertexDataType == EClothPatternVertexType::Render) ? Pattern.GetNumRenderFaces() : Pattern.GetNumSimFaces();
		TriIDs.SetNum(NumFaces);
		for (int32 TriIndex = 0; TriIndex < NumFaces; ++TriIndex)
		{
			TriIDs[TriIndex] = TriIndex;
		}

		//
		// Weight map layers precomputation
		//
		WeightMapNames = ClothFacade.GetWeightMapNames();

		// Set the reference skeleton if available
		const FString& SkeletonPathName = ClothFacade.GetLod(LodIndex).GetSkeletonAssetPathName();
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

	float GetVertexWeight(int32 LayerIndex, int32 VertexIndex) const
	{
		return Pattern.GetWeightMap(WeightMapNames[LayerIndex])[VertexIndex];
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
				const TConstArrayView<FVector3f> RenderPositions = Pattern.GetRenderPosition();
				return FVector3d(RenderPositions[VtxID]);
			}
			case EClothPatternVertexType::Sim2D:
			{
				const TConstArrayView<FVector2f> SimPositions = Pattern.GetSimPosition();
				const FVector2f& Pos = SimPositions[VtxID];
				return FVector3d(Pos[0], Pos[1], 0.0);
			}
			case EClothPatternVertexType::Sim3D:
			{
				const TConstArrayView<FVector3f> RestPositions = Pattern.GetSimRestPosition();
				const FVector3f& Pos = RestPositions[VtxID];
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
		FIntVector Face;		// Indices are using an LOD based indexation
		int32 VerticesOffset;	// and need to have the pattern offset removed

		if (VertexDataType == EClothPatternVertexType::Render)
		{
			Face = Pattern.GetRenderIndices()[TriID];
			VerticesOffset = Pattern.GetRenderVerticesOffset();
		}
		else
		{
			Face = Pattern.GetSimIndices()[TriID];
			VerticesOffset = Pattern.GetSimVerticesOffset();
		}

		VID0 = Face[0] - VerticesOffset;
		VID1 = Face[1] - VerticesOffset;
		VID2 = Face[2] - VerticesOffset;

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

	UE::AnimationCore::FBoneWeights GetVertexSkinWeight(int32 SkinWeightAttributeIndex, VertIDType VertexID) const 
	{
		using namespace UE::AnimationCore;
		
		checkfSlow(SkinWeightAttributeIndex == 0, TEXT("Cloth assets should only have one skin weight profile")); 

		const bool bGetRenderMeshData = (VertexDataType == EClothPatternVertexType::Render);
		const TConstArrayView<int32> NumBoneInfluences = bGetRenderMeshData ? Pattern.GetRenderNumBoneInfluences() : Pattern.GetSimNumBoneInfluences();
		const TConstArrayView<TArray<int32>> BoneIndices = bGetRenderMeshData ? Pattern.GetRenderBoneIndices() : Pattern.GetSimBoneIndices();
		const TConstArrayView<TArray<float>> BoneWeights = bGetRenderMeshData ? Pattern.GetRenderBoneWeights() : Pattern.GetSimBoneWeights();

		if (ensure(VertexID >= 0 && VertexID < NumBoneInfluences.Num()))
		{
			const int32 NumInfluences = NumBoneInfluences[VertexID];
			const TArray<int32> Indices = BoneIndices[VertexID];
			const TArray<float> Weights = BoneWeights[VertexID];
			
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
		return VertIDs; 
	}

	FVector3f GetNormal(NormalIDType ID) const 
	{ 
		return (VertexDataType == EClothPatternVertexType::Render) ? Pattern.GetRenderNormal()[ID] : Pattern.GetSimRestNormal()[ID];
	}

	bool GetNormalTri(const TriIDType& TriID, NormalIDType& ID0, NormalIDType& ID1, NormalIDType& ID2) const 
	{ 
		return GetTri(TriID, ID0, ID1, ID2); 
	}
	
	const TArray<int32>& GetUVIDs(int32 LayerID) const 
	{ 
		return (VertexDataType == EClothPatternVertexType::Render) ? VertIDs : EmptyArray;
	}

	FVector2f GetUV(int32 LayerID, UVIDType UVID) const 
	{ 
		checkf(VertexDataType == EClothPatternVertexType::Render, TEXT("Requested UVs from a Sim mesh"));
		const TConstArrayView<FVector2f> VertexUVs = Pattern.GetRenderUVs()[UVID];
		return VertexUVs[LayerID];
	}

	bool GetUVTri(int32 LayerID, const TriIDType& TriID, UVIDType& ID0, UVIDType& ID1, UVIDType& ID2) const 
	{ 
		return GetTri(TriID, ID0, ID1, ID2); 
	}
	
	const TArray<int32>& GetTangentIDs() const 
	{ 
		return (VertexDataType == EClothPatternVertexType::Render) ? VertIDs : EmptyArray;
	}

	FVector3f GetTangent(NormalIDType ID) const 
	{ 
		checkf(VertexDataType == EClothPatternVertexType::Render, TEXT("Requested Tangent from a Sim mesh"));
		return Pattern.GetRenderTangentU()[ID];
	}

	bool GetTangentTri(const TriIDType& TriID, NormalIDType& ID0, NormalIDType& ID1, NormalIDType& ID2) const 
	{ 
		return GetTri(TriID, ID0, ID1, ID2);
	}

	const TArray<int32>& GetBiTangentIDs() const 
	{ 
		return (VertexDataType == EClothPatternVertexType::Render) ? VertIDs : EmptyArray;
	}

	FVector3f GetBiTangent(NormalIDType ID) const 
	{
		checkf(VertexDataType == EClothPatternVertexType::Render, TEXT("Requested Bitangent from a Sim mesh"));
		return Pattern.GetRenderTangentV()[ID];
	}

	bool GetBiTangentTri(const TriIDType& TriID, NormalIDType& ID0, NormalIDType& ID1, NormalIDType& ID2) const
	{
		return GetTri(TriID, ID0, ID1, ID2);
	}

	const TArray<int32>& GetColorIDs() const 
	{ 
		return (VertexDataType == EClothPatternVertexType::Render) ? VertIDs : EmptyArray;
	}

	FVector4f GetColor(ColorIDType VID) const 
	{
		checkf(VertexDataType == EClothPatternVertexType::Render, TEXT("Requested color from a Sim mesh"));
		return Pattern.GetRenderColor()[VID];
	}

	bool GetColorTri(const TriIDType& TriID, ColorIDType& ID0, ColorIDType& ID1, ColorIDType& ID2) const 
	{
		return GetTri(TriID, ID0, ID1, ID2);
	}

private:

	const EClothPatternVertexType VertexDataType;

	TArray<TriIDType> TriIDs;		// indices into Pattern.GetSimFaces()
	TArray<VertIDType> VertIDs;		// indices into Pattern.GetSimVertices()
	
	TArray<FName> WeightMapNames;

	const FCollectionClothPatternConstFacade Pattern;

	TArray<int32> EmptyArray;

	const FReferenceSkeleton* RefSkeleton = nullptr;
};


void FClothPatternToDynamicMesh::Convert(const TSharedPtr<const FManagedArrayCollection> ClothCollection, int32 LODIndex, int32 PatternIndex, EClothPatternVertexType VertexDataType, UE::Geometry::FDynamicMesh3& MeshOut)
{
	const FCollectionClothConstFacade ClothFacade(ClothCollection);

	// Actual conversion
	UE::Geometry::TToDynamicMesh<FClothPatternWrapper> PatternToDynamicMesh; 
	FClothPatternWrapper PatternWrapper(ClothFacade, LODIndex, PatternIndex, VertexDataType);

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

		const FCollectionClothLodConstFacade LodFacade(ClothFacade.GetLod(LODIndex));
		const TArrayView<const int32> RenderMaterialIndex = LodFacade.GetRenderMaterialIndex();

		auto TriangleToMaterialFunction = [&RenderMaterialIndex](FClothPatternWrapper::TriIDType TriID)
		{
			if (ensure(RenderMaterialIndex.IsValidIndex(TriID)))
			{
				return RenderMaterialIndex[TriID];
			}
			return 0;
		};

		constexpr bool bCopyTangents = false;
		PatternToDynamicMesh.Convert(MeshOut, PatternWrapper, TriangleToGroupFunction, TriangleToMaterialFunction, bCopyTangents);
	}

}

void FClothPatternToDynamicMesh::Convert(const UChaosClothAsset* ClothAssetMeshIn, int32 LODIndex, int32 PatternIndex, EClothPatternVertexType VertexDataType, UE::Geometry::FDynamicMesh3& MeshOut)
{
	const TSharedPtr<const FManagedArrayCollection> ClothCollection = ClothAssetMeshIn->GetClothCollection();
	check(ClothCollection.IsValid());

	Convert(ClothCollection, LODIndex, PatternIndex, VertexDataType, MeshOut);
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

