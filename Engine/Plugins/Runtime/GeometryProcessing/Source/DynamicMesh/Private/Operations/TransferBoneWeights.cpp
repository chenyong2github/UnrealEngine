// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/TransferBoneWeights.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicVertexSkinWeightsAttribute.h"
#include "DynamicMesh/DynamicBoneAttribute.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Async/ParallelFor.h"
#include "Util/ProgressCancel.h"
#include "BoneIndices.h"
#include "BoneWeights.h"
#include "IndexTypes.h"
#include "TransformTypes.h"

using namespace UE::AnimationCore;
using namespace UE::Geometry;

namespace TransferBoneWeightsLocals 
{
	/**
	 * Given a triangle and point on a triangle (via barycentric coordinates), compute the bone weights for the point.
	 * 
	 * @param OutWeights Interpolated weights for a vertex with Bary barycentric coordinates
	 * @param TriVertices The vertices of a triangle containing the point we are interpolating the weights for
	 * @param Bary Barycentric coordinates of the point
	 * @param Attribute Attribute containing bone weights of the mesh that TriVertices belong to
	 * @param SourceIndexToBone Optional map from bone index to bone name for the source mesh
	 * @param TargetBoneToIndex OPtional map from bone name to bone index for the target mesh
	 * @param bNormalizeToOne If true, OutWeights will be normalized to sum to 1.
	 */
    void InterpolateBoneWeights(FBoneWeights& OutWeights,
								const FIndex3i& TriVertices,
								const FVector3f& Bary,
								const FDynamicMeshVertexSkinWeightsAttribute* Attribute,
								const TArray<FName>* SourceIndexToBone = nullptr,
								const TMap<FName, FBoneIndexType>* TargetBoneToIndex = nullptr,
								bool bNormalizeToOne = true)
	{
		FBoneWeights Weight1, Weight2, Weight3;
		Attribute->GetValue(TriVertices[0], Weight1);
		Attribute->GetValue(TriVertices[1], Weight2);
		Attribute->GetValue(TriVertices[2], Weight3);

		FBoneWeightsSettings BlendSettings;
		BlendSettings.SetNormalizeType(bNormalizeToOne ? EBoneWeightNormalizeType::Always : EBoneWeightNormalizeType::None);
		BlendSettings.SetBlendZeroInfluence(true);
		OutWeights = FBoneWeights::Blend(Weight1, Weight2, Weight3, Bary[0], Bary[1], Bary[2], BlendSettings);

		// Check if we need to remap the indices
		if (SourceIndexToBone && TargetBoneToIndex) 
		{
			FBoneWeightsSettings BoneSettings;
			BoneSettings.SetNormalizeType(EBoneWeightNormalizeType::None);

			FBoneWeights MappedWeights;

			for (int WeightIdx = 0; WeightIdx < OutWeights.Num(); ++WeightIdx)
			{
				const FBoneWeight& BoneWeight = OutWeights[WeightIdx];

				FBoneIndexType FromIdx = BoneWeight.GetBoneIndex();
				uint16 FromWeight = BoneWeight.GetRawWeight();

				checkSlow(FromIdx < SourceIndexToBone->Num());
				if (FromIdx < SourceIndexToBone->Num())
				{
					FName BoneName = (*SourceIndexToBone)[FromIdx];
					if (TargetBoneToIndex->Contains(BoneName))
					{
						FBoneIndexType ToIdx = (*TargetBoneToIndex)[BoneName];
						FBoneWeight MappedBoneWeight(ToIdx, FromWeight);
						MappedWeights.SetBoneWeight(MappedBoneWeight, BoneSettings);
					}
					else 
					{	
						UE_LOG(LogGeometry, Error, TEXT("FTransferBoneWeights: Bone name %s does not exist in the target mesh."), *BoneName.ToString());
					}
				}
			}


			if (MappedWeights.Num() == 0)
			{
				// If no bone mappings were found, add a single entry for the root bone
				MappedWeights.SetBoneWeight(FBoneWeight(0, 1.0f), FBoneWeightsSettings());
			}
			else if (OutWeights.Num() != MappedWeights.Num() && bNormalizeToOne)
			{	
				// In case some of the bones were not mapped we need to renormalize
				MappedWeights.Renormalize(FBoneWeightsSettings());
			}

			OutWeights = MappedWeights;
		}
	}

	
	FDynamicMeshVertexSkinWeightsAttribute* GetOrCreateSkinWeightsAttribute(FDynamicMesh3& InMesh, const FName& InProfileName)
	{
		checkSlow(InMesh.HasAttributes());
		FDynamicMeshVertexSkinWeightsAttribute* Attribute = InMesh.Attributes()->GetSkinWeightsAttribute(InProfileName);
		if (Attribute == nullptr)
		{
			Attribute = new FDynamicMeshVertexSkinWeightsAttribute(&InMesh);
			InMesh.Attributes()->AttachSkinWeightsAttribute(InProfileName, Attribute);
		}
		return Attribute;
	}
}

FTransferBoneWeights::FTransferBoneWeights(const FDynamicMesh3* InSourceMesh, 
								   const FName& InSourceProfileName,
								   const FDynamicMeshAABBTree3* InSourceBVH)
:
SourceMesh(InSourceMesh),
SourceProfileName(InSourceProfileName),
SourceBVH(InSourceBVH)
{
	// If the BVH for the source mesh was not specified then create one
	if (SourceBVH == nullptr)
	{
		InternalSourceBVH = MakeUnique<FDynamicMeshAABBTree3>(SourceMesh);
	}
}

FTransferBoneWeights::~FTransferBoneWeights() 
{
}

bool FTransferBoneWeights::Cancelled()
{
	return (Progress == nullptr) ? false : Progress->Cancelled();
}

EOperationValidationResult FTransferBoneWeights::Validate()
{	
	if (SourceMesh == nullptr) 
	{
		return EOperationValidationResult::Failed_UnknownReason;
	}

	// Either BVH was passed by the caller or was created internally in the constructor
	if (SourceBVH == nullptr && InternalSourceBVH.IsValid() == false) 
	{
		return EOperationValidationResult::Failed_UnknownReason;
	}

	if (SourceMesh->HasAttributes() == false) 
	{
		return EOperationValidationResult::Failed_UnknownReason;
	}
	
	if (SourceMesh->Attributes()->GetSkinWeightsAttribute(SourceProfileName) == nullptr) 
	{
		return EOperationValidationResult::Failed_UnknownReason;
	}

	if (bIgnoreBoneAttributes == false && SourceMesh->Attributes()->HasBones() == false) 
	{
		return EOperationValidationResult::Failed_UnknownReason;
	}

	return EOperationValidationResult::Ok;
}

bool FTransferBoneWeights::Compute(FDynamicMesh3& InOutTargetMesh, const FTransformSRT3d& InToWorld, const FName& InTargetProfileName)
{	
	if (Validate() != EOperationValidationResult::Ok) 
	{
		return false;
	}

	if (!InOutTargetMesh.HasAttributes())
	{
		InOutTargetMesh.EnableAttributes(); 
	}

	if (!bIgnoreBoneAttributes && !InOutTargetMesh.Attributes()->HasBones())
	{
		return false; // the target mesh must have bone attributes
	}
	
	FDynamicMeshVertexSkinWeightsAttribute* TargetSkinWeights = TransferBoneWeightsLocals::GetOrCreateSkinWeightsAttribute(InOutTargetMesh, InTargetProfileName);
	checkSlow(TargetSkinWeights);
	
	// Map the bone name to its index for the target mesh.
	// Will be null if either the target and the source skeletons are the same or the caller forced the attributes to be ignored
	TUniquePtr<TMap<FName, uint16>> TargetBoneToIndex;
	if (!bIgnoreBoneAttributes)
	{	
		const TArray<FName>& SourceBoneNames = SourceMesh->Attributes()->GetBoneNames()->GetAttribValues();
		const TArray<FName>& TargetBoneNames = InOutTargetMesh.Attributes()->GetBoneNames()->GetAttribValues();

		if (SourceBoneNames != TargetBoneNames)
		{
			TargetBoneToIndex = MakeUnique<TMap<FName, uint16>>();
			TargetBoneToIndex->Reserve(TargetBoneNames.Num());

			for (int BoneID = 0; BoneID < TargetBoneNames.Num(); ++BoneID)
			{
				const FName& BoneName = TargetBoneNames[BoneID];
				if (TargetBoneToIndex->Contains(BoneName))
				{
					checkSlow(false);
					return false; // there should be no duplicates
				}
				TargetBoneToIndex->Add(BoneName, static_cast<uint16>(BoneID));
			}
		}
	}

	bool bFailed = false;
	
	ParallelFor(InOutTargetMesh.MaxVertexID(), [&](int32 VertexID)
	{
		if (Cancelled() || bFailed) 
		{
			return;
		}
		
		if (InOutTargetMesh.IsVertex(VertexID)) 
		{
			FVector3d Point = InOutTargetMesh.GetVertex(VertexID);
		
			FBoneWeights Weights;
			if (Compute(Point, InToWorld, Weights, TargetBoneToIndex.Get()) == false)
			{
				bFailed = true;
				return;
			}
			
			TargetSkinWeights->SetValue(VertexID, Weights);
		}

	}, bUseParallel ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread);

	if (Cancelled() || bFailed) 
	{
		return false;
	}
		
	return true;
}

bool FTransferBoneWeights::Compute(const FVector3d& InPoint, const FTransformSRT3d& InToWorld, FBoneWeights& OutWeights, const TMap<FName, uint16>* TargetBoneToIndex) 
{
	const FDynamicMeshVertexSkinWeightsAttribute* SourceSkinWeights = SourceMesh->Attributes()->GetSkinWeightsAttribute(SourceProfileName);
	checkSlow(SourceSkinWeights);

	const TArray<FName>* SourceBoneNames = nullptr;
	if (!bIgnoreBoneAttributes) 
	{
		SourceBoneNames = &SourceMesh->Attributes()->GetBoneNames()->GetAttribValues();
	}

	IMeshSpatial::FQueryOptions Options;
	double NearestDistSqr;
	int32 NearTriID;
	
	const FVector3d WorldPoint = InToWorld.TransformPosition(InPoint);
	if (SourceBVH != nullptr) 
	{ 
		NearTriID = SourceBVH->FindNearestTriangle(WorldPoint, NearestDistSqr, Options);
	}
	else 
	{
		NearTriID = InternalSourceBVH->FindNearestTriangle(WorldPoint, NearestDistSqr, Options);
	}

	if (!ensure(NearTriID != IndexConstants::InvalidID))
	{
		return false;
	}

	const FDistPoint3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleDistance(*SourceMesh, NearTriID, WorldPoint);
	const FVector3d NearestPnt = Query.ClosestTrianglePoint;
	const FIndex3i TriVertex = SourceMesh->GetTriangle(NearTriID);

	const FVector3d Bary = VectorUtil::BarycentricCoords(NearestPnt,
														 SourceMesh->GetVertexRef(TriVertex.A),
														 SourceMesh->GetVertexRef(TriVertex.B),
														 SourceMesh->GetVertexRef(TriVertex.C));

	TransferBoneWeightsLocals::InterpolateBoneWeights(OutWeights,
													  TriVertex,
													  FVector3f((float)Bary[0], (float)Bary[1], (float)Bary[2]),
													  SourceSkinWeights,
													  SourceBoneNames,
													  TargetBoneToIndex);

	return true;
}