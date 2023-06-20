// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/TransferSkinWeightsNode.h"
#include "ChaosClothAsset/ClothDataflowTools.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "Animation/Skeleton.h"
#include "Dataflow/DataflowInputOutput.h"
#include "DynamicMesh/DynamicBoneAttribute.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicVertexSkinWeightsAttribute.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/MeshTransforms.h"
#include "Engine/SkeletalMesh.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "Operations/TransferBoneWeights.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "SkeletalMeshAttributes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TransferSkinWeightsNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetTransferSkinWeightsNode"

namespace UE::Chaos::ClothAsset::Private
{
	static void SkeletalMeshToDynamicMesh(USkeletalMesh* FromSkeletalMeshAsset, int32 LodIndex, FDynamicMesh3& ToDynamicMesh)
	{	
		FMeshDescription SourceMesh;
		FromSkeletalMeshAsset->GetMeshDescription(LodIndex, SourceMesh);
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(&SourceMesh, ToDynamicMesh);
	}

	// Convert the cloth into DynamicMesh
	// @todo: This should instead be handled by a cloth lod to dynamic mesh converter similar to functions in the ClothPatternToDynamicMesh.h
	static bool SimClothToDynamicMesh(
		const UE::Chaos::ClothAsset::FCollectionClothConstFacade& ClothFacade,
		const FReferenceSkeleton& TargetRefSkeleton, // the reference skeleton to add to the dynamic mesh
		UE::Geometry::FDynamicMesh3& WeldedSimMesh)
	{
		using namespace UE::Geometry;

		// Convert the sim mesh to DynamicMesh. 
		// @todo: FTransferBoneWeights should accept raw data arrays (vertices/triangles) to avoid this conversion
		WeldedSimMesh.Clear();
		for (const FVector3f& Pos : ClothFacade.GetSimPosition3D())
		{
			WeldedSimMesh.AppendVertex(FVector3d(Pos));
		}

		for (const FIntVector& Indices : ClothFacade.GetSimIndices3D())
		{
			const int TID = WeldedSimMesh.AppendTriangle(Indices[0], Indices[1], Indices[2]);
			if (TID < 0) // Failed to add the triangle (non-manifold/duplicate)
			{
				return false;
			}
		}
		
		WeldedSimMesh.EnableAttributes();
		FMeshNormals NormalsUtil(&WeldedSimMesh);
		NormalsUtil.InitializeOverlayToPerVertexNormals(WeldedSimMesh.Attributes()->PrimaryNormals());

		// Setup the skeleton.
		// @note we can't simply copy the bone attributes from the source USkeletalMesh because the cloth  
		// asset reference skeleton comes from the USkeleton not the USkeletalMesh
		WeldedSimMesh.Attributes()->EnableBones(TargetRefSkeleton.GetRawBoneNum());
		for (int32 BoneIdx = 0; BoneIdx < TargetRefSkeleton.GetRawBoneNum(); ++BoneIdx)
		{
			WeldedSimMesh.Attributes()->GetBoneNames()->SetValue(BoneIdx, TargetRefSkeleton.GetRawRefBoneInfo()[BoneIdx].Name);
		}

		return true;
	}

	static void TransferInpaintWeights(
		const FReferenceSkeleton& TargetRefSkeleton,
		const double NormalThreshold,
		const double RadiusPercentage,
		bool bUseParallel,
		UE::Chaos::ClothAsset::FCollectionClothFacade& ClothFacade,
		UE::Geometry::FTransferBoneWeights& TransferBoneWeights)
	{
		using namespace UE::Geometry;

		//
		// Convert cloth sim mesh LOD to the welded dynamic sim mesh.
		//
		FDynamicMesh3 WeldedSimMesh;
		if (!ensure(SimClothToDynamicMesh(ClothFacade, TargetRefSkeleton, WeldedSimMesh)))
		{
			UE_LOG(LogChaosClothAssetDataflowNodes, Warning, TEXT("TransferSkinWeightsNode: Failed to weld the simulation mesh for LOD."));
			return;
		}

		//
		// Transfer the weights from the body to the welded sim mesh.
		//
		TransferBoneWeights.NormalThreshold = FMathd::DegToRad * NormalThreshold;
		TransferBoneWeights.SearchRadius = RadiusPercentage * WeldedSimMesh.GetBounds().DiagonalLength();
		if (!ensure(TransferBoneWeights.Validate() == EOperationValidationResult::Ok))
		{
			UE_LOG(LogChaosClothAssetDataflowNodes, Warning, TEXT("TransferSkinWeightsNode: Transfer method parameters are invalid."));
			return;
		}
		if (!ensure(TransferBoneWeights.TransferWeightsToMesh(WeldedSimMesh, FSkeletalMeshAttributes::DefaultSkinWeightProfileName)))
		{
			UE_LOG(LogChaosClothAssetDataflowNodes, Warning, TEXT("TransferSkinWeightsNode: Transferring skin weights failed."));
			return;
		}

		//
		// Copy the new bone weight data from the welded sim mesh back to the cloth patterns.
		//
		ParallelFor(WeldedSimMesh.MaxVertexID(), [&ClothFacade, &WeldedSimMesh](int32 WeldedID)
		{
			const FDynamicMeshVertexSkinWeightsAttribute* OutAttribute = WeldedSimMesh.Attributes()->GetSkinWeightsAttribute(FSkeletalMeshAttributes::DefaultSkinWeightProfileName);

			checkSlow(OutAttribute);
			checkSlow(WeldedSimMesh.IsVertex(WeldedID));
			checkSlow(WeldedID < ClothFacade.GetNumSimVertices3D());
			OutAttribute->GetValue(WeldedID, ClothFacade.GetSimBoneIndices()[WeldedID],
				ClothFacade.GetSimBoneWeights()[WeldedID]);
		}, bUseParallel ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread);

		//
		// Compute the bone weights for the render mesh by transferring weights from the sim mesh
		// @todo If render mesh eventually supports welding, we should be transferring weights from the 
		// body instead, same as we do for the sim mesh.
		//
		FTransferBoneWeights SimToRenderMeshTransfer(&WeldedSimMesh, FSkeletalMeshAttributes::DefaultSkinWeightProfileName);
		SimToRenderMeshTransfer.bUseParallel = bUseParallel;
		SimToRenderMeshTransfer.TransferMethod = FTransferBoneWeights::ETransferBoneWeightsMethod::ClosestPointOnSurface;
		ParallelFor(ClothFacade.GetNumRenderVertices(), [&SimToRenderMeshTransfer, &ClothFacade](int32 VertexID)
		{
			SimToRenderMeshTransfer.TransferWeightsToPoint(ClothFacade.GetRenderBoneIndices()[VertexID],
				ClothFacade.GetRenderBoneWeights()[VertexID],
				ClothFacade.GetRenderPosition()[VertexID]);

		}, bUseParallel ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread);
	}

	static void TransferClosestPointOnSurface(
		const FReferenceSkeleton& TargetRefSkeleton,
		const bool bUseParallel,
		UE::Chaos::ClothAsset::FCollectionClothFacade& ClothFacade,
		UE::Geometry::FTransferBoneWeights& TransferBoneWeights)
	{
		using namespace UE::Geometry;

		//
		// Compute the bone index mappings. This allows the transfer operator to retarget weights to the correct skeleton.
		//
		TMap<FName, FBoneIndexType> TargetBoneToIndex;
		TargetBoneToIndex.Reserve(TargetRefSkeleton.GetRawBoneNum());
		for (int32 BoneIdx = 0; BoneIdx < TargetRefSkeleton.GetRawBoneNum(); ++BoneIdx)
		{
			TargetBoneToIndex.Add(TargetRefSkeleton.GetRawRefBoneInfo()[BoneIdx].Name, BoneIdx);
		}

		if (!ensure(TransferBoneWeights.Validate() == EOperationValidationResult::Ok))
		{
			UE_LOG(LogChaosClothAssetDataflowNodes, Warning, TEXT("TransferSkinWeightsNode: Transfer method parameters are invalid."));
			return;
		}

		//
		// Transfer weights to the sim mesh.
		//
		ParallelFor(ClothFacade.GetNumSimVertices3D(), [&TransferBoneWeights, &TargetBoneToIndex, &ClothFacade](int32 VertexID)
		{
			TransferBoneWeights.TransferWeightsToPoint(ClothFacade.GetSimBoneIndices()[VertexID],
				ClothFacade.GetSimBoneWeights()[VertexID],
				ClothFacade.GetSimPosition3D()[VertexID],
				&TargetBoneToIndex);

		}, bUseParallel ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread);

		//
		// Transfer weights to the render mesh.
		//
		ParallelFor(ClothFacade.GetNumRenderVertices(), [&TransferBoneWeights, &TargetBoneToIndex, &ClothFacade](int32 VertexID)
		{
			TransferBoneWeights.TransferWeightsToPoint(ClothFacade.GetRenderBoneIndices()[VertexID],
				ClothFacade.GetRenderBoneWeights()[VertexID],
				ClothFacade.GetRenderPosition()[VertexID],
				&TargetBoneToIndex);

		}, bUseParallel ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread);
	}
}



FChaosClothAssetTransferSkinWeightsNode::FChaosClothAssetTransferSkinWeightsNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterOutputConnection(&Collection, &Collection);
}

void FChaosClothAssetTransferSkinWeightsNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	using namespace UE::Chaos::ClothAsset;
	using namespace UE::Chaos::ClothAsset::Private;
	using namespace UE::Geometry;

	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		// Evaluate inputs
		FManagedArrayCollection InputCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(InputCollection));

		if (SkeletalMesh && FCollectionClothFacade(ClothCollection).IsValid())  // Can only act on the collection if it is a valid cloth collection
		{
			if (!SkeletalMesh->IsValidLODIndex(LodIndex))
			{
				FClothDataflowTools::LogAndToastWarning(*this,
					LOCTEXT("InvalidLodIndexHeadline", "Invalid LOD Index."),
					FText::Format(
						LOCTEXT("InvalidLodIndexDetails", "LOD index {0} is not a valid LOD for skeletal mesh {1}."),
						LodIndex,
						FText::FromString(SkeletalMesh.GetName())));
				SetValue(Context, MoveTemp(*ClothCollection), &Collection);
				return;
			}

			//
			// Convert source Skeletal Mesh to Dynamic Mesh.
			//
			FDynamicMesh3 SourceDynamicMesh;
			SkeletalMeshToDynamicMesh(SkeletalMesh, LodIndex, SourceDynamicMesh);
			MeshTransforms::ApplyTransform(SourceDynamicMesh, Transform, true);
			const FReferenceSkeleton& TargetRefSkeleton = SkeletalMesh->GetSkeleton()->GetReferenceSkeleton();

			//
			// Setup the bone weight transfer operator for the source mesh.
			//
			constexpr bool bUseParallel = true;
			FTransferBoneWeights TransferBoneWeights(&SourceDynamicMesh, FSkeletalMeshAttributes::DefaultSkinWeightProfileName);
			TransferBoneWeights.bUseParallel = bUseParallel;
			TransferBoneWeights.TransferMethod = static_cast<FTransferBoneWeights::ETransferBoneWeightsMethod>(TransferMethod);

			//
			// Transfer the bone weights from the source Skeletal mesh to the Cloth asset.
			//
			FCollectionClothFacade ClothFacade(ClothCollection);
			ClothFacade.SetSkeletonAssetPathName(SkeletalMesh->GetSkeleton()->GetPathName());

			if (TransferMethod == EChaosClothAssetTransferSkinWeightsMethod::InpaintWeights)
			{
				TransferInpaintWeights(TargetRefSkeleton, NormalThreshold, RadiusPercentage, bUseParallel, ClothFacade, TransferBoneWeights);
			}
			else if (TransferMethod == EChaosClothAssetTransferSkinWeightsMethod::ClosestPointOnSurface)
			{
				TransferClosestPointOnSurface(TargetRefSkeleton, bUseParallel, ClothFacade, TransferBoneWeights);
			}
			else
			{
				checkNoEntry();
			}
		}

		SetValue(Context, MoveTemp(*ClothCollection), &Collection);
	}
}

#undef LOCTEXT_NAMESPACE
