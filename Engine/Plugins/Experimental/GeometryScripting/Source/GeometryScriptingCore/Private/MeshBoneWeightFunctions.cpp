// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/MeshBoneWeightFunctions.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicVertexSkinWeightsAttribute.h"
#include "UDynamicMesh.h"
#include "BoneWeights.h"

using namespace UE::Geometry;
using namespace UE::AnimationCore;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_MeshBoneWeightFunctions"


template<typename ReturnType> 
ReturnType SimpleMeshBoneWeightQuery(
	UDynamicMesh* Mesh, FGeometryScriptBoneWeightProfile Profile, 
	bool& bIsValidBoneWeights, ReturnType DefaultValue, 
	TFunctionRef<ReturnType(const FDynamicMesh3& Mesh, const FDynamicMeshVertexSkinWeightsAttribute& SkinWeights)> QueryFunc)
{
	bIsValidBoneWeights = false;
	ReturnType RetVal = DefaultValue;
	if (Mesh)
	{
		Mesh->ProcessMesh([&](const FDynamicMesh3& ReadMesh)
		{
			if (ReadMesh.HasAttributes())
			{
				if ( const FDynamicMeshVertexSkinWeightsAttribute* BoneWeights = ReadMesh.Attributes()->GetSkinWeightsAttribute(Profile.GetProfileName()) )
				{
					bIsValidBoneWeights = true;
					RetVal = QueryFunc(ReadMesh, *BoneWeights);
				}
			}
		});
	}
	return RetVal;
}


template<typename ReturnType> 
ReturnType SimpleMeshBoneWeightEdit(
	UDynamicMesh* Mesh, FGeometryScriptBoneWeightProfile Profile, 
	bool& bIsValidBoneWeights, ReturnType DefaultValue, 
	TFunctionRef<ReturnType(FDynamicMesh3& Mesh, FDynamicMeshVertexSkinWeightsAttribute& SkinWeights)> EditFunc)
{
	bIsValidBoneWeights = false;
	ReturnType RetVal = DefaultValue;
	if (Mesh)
	{
		Mesh->EditMesh([&](FDynamicMesh3& ReadMesh)
		{
			if (ReadMesh.HasAttributes())
			{
				if ( FDynamicMeshVertexSkinWeightsAttribute* BoneWeights = ReadMesh.Attributes()->GetSkinWeightsAttribute(Profile.GetProfileName()) )
				{
					bIsValidBoneWeights = true;
					RetVal = EditFunc(ReadMesh, *BoneWeights);
				}
			}
		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);
	}
	return RetVal;
}




UDynamicMesh* UGeometryScriptLibrary_MeshBoneWeightFunctions::MeshHasBoneWeights(
	UDynamicMesh* TargetMesh,
	bool& bHasBoneWeights,
	FGeometryScriptBoneWeightProfile Profile)
{
	bool bHasBoneWeightProfile = false;
	bool bOK = SimpleMeshBoneWeightQuery<bool>(TargetMesh, Profile, bHasBoneWeightProfile, false,
		[&](const FDynamicMesh3& Mesh, const FDynamicMeshVertexSkinWeightsAttribute& SkinWeights) { return true; });
	bHasBoneWeights = bHasBoneWeightProfile;
	return TargetMesh;
}


UDynamicMesh* UGeometryScriptLibrary_MeshBoneWeightFunctions::GetMaxBoneWeightIndex(
	UDynamicMesh* TargetMesh,
	bool& bHasBoneWeights,
	int& MaxBoneIndex,
	FGeometryScriptBoneWeightProfile Profile)
{
	MaxBoneIndex = -1;
	bool bHasBoneWeightProfile = false;
	bool bOK = SimpleMeshBoneWeightQuery<bool>(TargetMesh, Profile, bHasBoneWeightProfile, false,
		[&](const FDynamicMesh3& Mesh, const FDynamicMeshVertexSkinWeightsAttribute& SkinWeights) 
		{ 
			for (int32 VertexID : Mesh.VertexIndicesItr())
			{
				FBoneWeights BoneWeights;
				SkinWeights.GetValue(VertexID, BoneWeights);
				int32 Num = BoneWeights.Num();
				for (int32 k = 0; k < Num; ++k)
				{
					MaxBoneIndex = FMathd::Max(MaxBoneIndex, BoneWeights[k].GetBoneIndex());
				}
			}
			return true;
		});
	bHasBoneWeights = bHasBoneWeightProfile;
	return TargetMesh;
}



UDynamicMesh* UGeometryScriptLibrary_MeshBoneWeightFunctions::GetVertexBoneWeights(
	UDynamicMesh* TargetMesh,
	int VertexID,
	TArray<FGeometryScriptBoneWeight>& BoneWeightsOut,
	bool& bHasValidBoneWeights,
	FGeometryScriptBoneWeightProfile Profile)
{
	bool bHasBoneWeightProfile = false;
	bHasValidBoneWeights = SimpleMeshBoneWeightQuery<bool>(TargetMesh, Profile, bHasBoneWeightProfile, false,
		[&](const FDynamicMesh3& Mesh, const FDynamicMeshVertexSkinWeightsAttribute& SkinWeights)
	{
		if (Mesh.IsVertex(VertexID))
		{
			FBoneWeights BoneWeights;
			SkinWeights.GetValue(VertexID, BoneWeights);
			int32 Num = BoneWeights.Num();
			BoneWeightsOut.SetNum(Num);
			for (int32 k = 0; k < Num; ++k)
			{
				FGeometryScriptBoneWeight NewBoneWeight;
				NewBoneWeight.BoneIndex = BoneWeights[k].GetBoneIndex();
				NewBoneWeight.Weight = BoneWeights[k].GetWeight();
				BoneWeightsOut.Add(NewBoneWeight);
			}
		}
		return BoneWeightsOut.Num() > 0;
	});

	return TargetMesh;
}


UDynamicMesh* UGeometryScriptLibrary_MeshBoneWeightFunctions::GetLargestVertexBoneWeight(
	UDynamicMesh* TargetMesh,
	int VertexID,
	FGeometryScriptBoneWeight& BoneWeight,
	bool& bHasValidBoneWeights,
	FGeometryScriptBoneWeightProfile Profile)
{
	bHasValidBoneWeights = false;
	bool bHasBoneWeightProfile = false;
	FBoneWeight FoundMax = SimpleMeshBoneWeightQuery<FBoneWeight>(TargetMesh, Profile, bHasBoneWeightProfile, FBoneWeight(),
	[&](const FDynamicMesh3& Mesh, const FDynamicMeshVertexSkinWeightsAttribute& SkinWeights) 
	{ 
		FBoneWeight MaxBoneWeight = FBoneWeight();
		if (Mesh.IsVertex(VertexID))
		{
			bHasValidBoneWeights = true;
			float MaxWeight = 0;
			FBoneWeights BoneWeights;
			SkinWeights.GetValue(VertexID, BoneWeights);
			int32 Num = BoneWeights.Num();
			for (int32 k = 0; k < Num; ++k)
			{
				const FBoneWeight& BoneWeight = BoneWeights[k];
				if (BoneWeight.GetWeight() > MaxWeight)
				{
					MaxWeight = BoneWeight.GetWeight();
					MaxBoneWeight = BoneWeight;
				}
			}
		}
		else
		{
			UE_LOG(LogGeometry, Warning, TEXT("GetLargestMeshBoneWeight: VertexID %d does not exist"), VertexID);
		}
		return MaxBoneWeight;
	});
	
	if (bHasValidBoneWeights)
	{
		BoneWeight.BoneIndex = FoundMax.GetBoneIndex();
		BoneWeight.Weight = FoundMax.GetWeight();
	}

	return TargetMesh;
}





UDynamicMesh* UGeometryScriptLibrary_MeshBoneWeightFunctions::SetVertexBoneWeights(
	UDynamicMesh* TargetMesh,
	int VertexID,
	const TArray<FGeometryScriptBoneWeight>& BoneWeights,
	bool& bHasValidBoneWeights,
	FGeometryScriptBoneWeightProfile Profile)
{
	bool bHasBoneWeightProfile = false;
	bHasValidBoneWeights = SimpleMeshBoneWeightEdit<bool>(TargetMesh, Profile, bHasBoneWeightProfile, false,
		[&](FDynamicMesh3& Mesh, FDynamicMeshVertexSkinWeightsAttribute& SkinWeights)
	{
		if (Mesh.IsVertex(VertexID))
		{
			int32 Num = BoneWeights.Num();
			TArray<FBoneWeight> NewWeightsList;
			for (int32 k = 0; k < Num; ++k)
			{
				FBoneWeight NewWeight;
				NewWeight.SetBoneIndex(BoneWeights[k].BoneIndex);
				NewWeight.SetWeight(BoneWeights[k].Weight);
				NewWeightsList.Add(NewWeight);
			}
			FBoneWeights NewBoneWeights = FBoneWeights::Create(NewWeightsList);
			SkinWeights.SetValue(VertexID, NewBoneWeights);
			return true;
		}
		return false;
	});

	return TargetMesh;
}




#undef LOCTEXT_NAMESPACE