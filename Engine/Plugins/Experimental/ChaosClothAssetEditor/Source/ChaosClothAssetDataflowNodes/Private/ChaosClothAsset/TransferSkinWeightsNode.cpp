// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/TransferSkinWeightsNode.h"
#include "ChaosClothAsset/ClothPatternToDynamicMesh.h"
#include "ChaosClothAsset/ClothDataflowTools.h"
#include "ChaosClothAsset/ClothGeometryTools.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "Animation/Skeleton.h"
#include "Dataflow/DataflowInputOutput.h"
#include "DynamicMesh/DynamicBoneAttribute.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicVertexSkinWeightsAttribute.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMesh/NonManifoldMappingSupport.h"
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
		const TSharedRef<FManagedArrayCollection>& ClothCollection,
		const FReferenceSkeleton& TargetRefSkeleton, // the reference skeleton to add to the dynamic mesh
		UE::Geometry::FDynamicMesh3& WeldedSimMesh)
	{
		using namespace UE::Geometry;
		using namespace UE::Chaos::ClothAsset;

		// Convert the sim mesh to DynamicMesh. 
		// @todo: FTransferBoneWeights should accept raw data arrays (vertices/triangles) to avoid this conversion
		FClothPatternToDynamicMesh PatternToDynamicMesh;
		constexpr bool bDisableAttributes = false;
		PatternToDynamicMesh.Convert(ClothCollection, INDEX_NONE, EClothPatternVertexType::Sim3D, WeldedSimMesh, bDisableAttributes);

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

	static bool TransferInpaintWeights(
		const FReferenceSkeleton& TargetRefSkeleton,
		const double NormalThreshold,
		const double RadiusPercentage,
		const int32 NumSmoothingIterations,
		const double SmoothingStrength,
		bool bUseParallel,
		const TSharedRef<FManagedArrayCollection>& ClothCollection,
		const FString& InpaintMaskWeightMapName,
		UE::Geometry::FTransferBoneWeights& TransferBoneWeights)
	{
		using namespace UE::Geometry;

		//
		// Convert cloth sim mesh LOD to the welded dynamic sim mesh.
		//
		
		// We cannot handle it if there are orphaned vertices, so strip those now.
		UE::Chaos::ClothAsset::FClothGeometryTools::CleanupAndCompactMesh(ClothCollection);

		FDynamicMesh3 WeldedSimMesh;
		if (!ensure(SimClothToDynamicMesh(ClothCollection, TargetRefSkeleton, WeldedSimMesh)))
		{
			UE_LOG(LogChaosClothAssetDataflowNodes, Warning, TEXT("TransferSkinWeightsNode: Failed to weld the simulation mesh for LOD."));
			return false;
		}

		//
		// Transfer the weights from the body to the welded sim mesh.
		//
		TransferBoneWeights.NormalThreshold = FMathd::DegToRad * NormalThreshold;
		TransferBoneWeights.SearchRadius = RadiusPercentage * WeldedSimMesh.GetBounds().DiagonalLength();
		TransferBoneWeights.NumSmoothingIterations = NumSmoothingIterations;
		TransferBoneWeights.SmoothingStrength = SmoothingStrength;
		TransferBoneWeights.LayeredMeshSupport = true; // multilayerd clothing
		TransferBoneWeights.ForceInpaintWeightMapName = FName(InpaintMaskWeightMapName);

		if (!ensure(TransferBoneWeights.Validate() == EOperationValidationResult::Ok))
		{
			UE_LOG(LogChaosClothAssetDataflowNodes, Warning, TEXT("TransferSkinWeightsNode: Transfer method parameters are invalid."));
			return false;
		}
		if (!ensure(TransferBoneWeights.TransferWeightsToMesh(WeldedSimMesh, FSkeletalMeshAttributes::DefaultSkinWeightProfileName)))
		{
			UE_LOG(LogChaosClothAssetDataflowNodes, Warning, TEXT("TransferSkinWeightsNode: Transferring skin weights failed."));
			return false;
		}

		UE::Chaos::ClothAsset::FCollectionClothFacade ClothFacade(ClothCollection);
		const FName InpaintWeightMaskName = TEXT("_InpaintWeightMask");
		ClothFacade.AddWeightMap(InpaintWeightMaskName);
		TArrayView<float> InpaintWeightMask = ClothFacade.GetWeightMap(InpaintWeightMaskName);

		//
		// Copy the new bone weight data from the welded sim mesh back to the cloth patterns.
		//
		const FNonManifoldMappingSupport NonManifoldMapping(WeldedSimMesh);
		TArray<TArray<int32>> SimMeshToDynamicMesh;
		if (NonManifoldMapping.IsNonManifoldVertexInSource())
		{
			// WeldedSimMesh indices don't match cloth collection. 
			SimMeshToDynamicMesh.SetNum(ClothFacade.GetNumSimVertices3D());
			for (int32 DynamicMeshVert = 0; DynamicMeshVert < WeldedSimMesh.VertexCount(); ++DynamicMeshVert)
			{
				SimMeshToDynamicMesh[NonManifoldMapping.GetOriginalNonManifoldVertexID(DynamicMeshVert)].Add(DynamicMeshVert);
			}

			ParallelFor(ClothFacade.GetNumSimVertices3D(), [&ClothFacade, &WeldedSimMesh, &SimMeshToDynamicMesh, &TransferBoneWeights, &InpaintWeightMask](int32 SimVertexID)
			{
				const FDynamicMeshVertexSkinWeightsAttribute* const OutAttribute = WeldedSimMesh.Attributes()->GetSkinWeightsAttribute(FSkeletalMeshAttributes::DefaultSkinWeightProfileName);
				checkSlow(OutAttribute);
				if (!ensure(SimMeshToDynamicMesh[SimVertexID].Num() > 0))
				{
					ClothFacade.GetSimBoneIndices()[SimVertexID].Reset();
					ClothFacade.GetSimBoneWeights()[SimVertexID].Reset();
					return;
				}
				if (SimMeshToDynamicMesh[SimVertexID].Num() == 1)
				{
					// Simple most common case, one-to-one correspondence. Just copy over.
					const int32 WeldedID = SimMeshToDynamicMesh[SimVertexID][0];
					checkSlow(WeldedSimMesh.IsVertex(WeldedID));
					OutAttribute->GetValue(WeldedID, ClothFacade.GetSimBoneIndices()[SimVertexID],
						ClothFacade.GetSimBoneWeights()[SimVertexID]);
					InpaintWeightMask[SimVertexID] = TransferBoneWeights.MatchedVertices[WeldedID] ? 1.0f : 0.0f;
				}
				else
				{
					// TODO: The logic below should be moved to BoneWeights.h and implemented as Merge method which takes
					// an array of bone weights and computes their average.
					
					// Need to merge data because dynamic mesh split the original vertex
					TMap<int32, TPair<float, int32>> CombinedData;
					for (const int32 WeldedID : SimMeshToDynamicMesh[SimVertexID])
					{
						TArray<int32> Indices;
						TArray<float> Weights;
						checkSlow(WeldedSimMesh.IsVertex(WeldedID));
						OutAttribute->GetValue(WeldedID, Indices, Weights);
						check(Indices.Num() == Weights.Num());
						for (int32 Idx = 0; Idx < Indices.Num(); ++Idx)
						{
							TPair<float, int32>& WeightedFloat = CombinedData.FindOrAdd(Indices[Idx]);
							WeightedFloat.Get<0>() += Weights[Idx];
							WeightedFloat.Get<1>() += 1;
						}
					}
					TArray<int32>& BoneIndices = ClothFacade.GetSimBoneIndices()[SimVertexID];
					TArray<float>& BoneWeights = ClothFacade.GetSimBoneWeights()[SimVertexID];
					BoneIndices.Reset(CombinedData.Num());
					BoneWeights.Reset(CombinedData.Num());
					float WeightsSum = 0.f;
					for (TMap<int32, TPair<float, int32>>::TConstIterator CombinedDataIter = CombinedData.CreateConstIterator(); CombinedDataIter; ++CombinedDataIter)
					{
						check(CombinedDataIter.Value().Get<1>() > 0);
						BoneIndices.Add(CombinedDataIter.Key());
						const float FloatVal = CombinedDataIter.Value().Get<0>() / (float)CombinedDataIter.Value().Get<1>();
						BoneWeights.Add(FloatVal);
						WeightsSum += FloatVal;
					}
					if (BoneIndices.Num() > MAX_TOTAL_INFLUENCES)
					{
						// Choose MAX_TOTAL_INFLUENCES highest weighted bones.
						TArray<TPair<float, int32>> SortableData;
						SortableData.Reserve(BoneIndices.Num());
						for (int32 Idx = 0; Idx < BoneIndices.Num(); ++Idx)
						{
							SortableData.Emplace(BoneWeights[Idx], BoneIndices[Idx]);
						}
						SortableData.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A > B; });

						BoneIndices.SetNum(MAX_TOTAL_INFLUENCES);
						BoneWeights.SetNum(MAX_TOTAL_INFLUENCES);
						WeightsSum = 0.f;
						for (int32 Idx = 0; Idx < MAX_TOTAL_INFLUENCES; ++Idx)
						{
							BoneIndices[Idx] = SortableData[Idx].Get<1>();
							BoneWeights[Idx] = SortableData[Idx].Get<0>();
							WeightsSum += SortableData[Idx].Get<0>();
						}
					}

					// Normalize weights
					const float WeightsSumRecip = WeightsSum > UE_SMALL_NUMBER ? 1.f / WeightsSum : 0.f;
					for (float& Weight : BoneWeights)
					{
						Weight *= WeightsSumRecip;
					}

					const int32 WeldedID = SimMeshToDynamicMesh[SimVertexID][0]; // Any welded id can be used here
					InpaintWeightMask[SimVertexID] = TransferBoneWeights.MatchedVertices[WeldedID] ? 1.0f : 0.0f;
				}
			}, bUseParallel ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread);
		}
		else
		{

			ParallelFor(WeldedSimMesh.MaxVertexID(), [&ClothFacade, &WeldedSimMesh, &NonManifoldMapping, &InpaintWeightMask, &TransferBoneWeights](int32 WeldedID)
			{
				const FDynamicMeshVertexSkinWeightsAttribute* OutAttribute = WeldedSimMesh.Attributes()->GetSkinWeightsAttribute(FSkeletalMeshAttributes::DefaultSkinWeightProfileName);

				checkSlow(OutAttribute);
				checkSlow(WeldedSimMesh.IsVertex(WeldedID));
				checkSlow(WeldedID < ClothFacade.GetNumSimVertices3D());
				OutAttribute->GetValue(WeldedID, ClothFacade.GetSimBoneIndices()[WeldedID],
					ClothFacade.GetSimBoneWeights()[WeldedID]);
				InpaintWeightMask[WeldedID] = TransferBoneWeights.MatchedVertices[WeldedID] ? 1.0f : 0.0f;
			}, bUseParallel ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread);
		}

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

		return true;
	}

	static bool TransferClosestPointOnSurface(
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
			return false;
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

		return true;
	}
}



FChaosClothAssetTransferSkinWeightsNode::FChaosClothAssetTransferSkinWeightsNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterOutputConnection(&Collection, &Collection);
	RegisterInputConnection(&InpaintMask.WeightMap, GET_MEMBER_NAME_CHECKED(FChaosClothAssetWeightedValueNonAnimatableNoLowHighRange, WeightMap));
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

			InpaintMask.WeightMap_Override = GetValue<FString>(Context, &InpaintMask.WeightMap, FString());

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

			bool bTransferResult = false;
			if (TransferMethod == EChaosClothAssetTransferSkinWeightsMethod::InpaintWeights)
			{
				const FString& InpainMaskWeightMapName = GetValue<FString>(Context, &InpaintMask.WeightMap);
				bTransferResult = TransferInpaintWeights(TargetRefSkeleton, NormalThreshold, RadiusPercentage, NumSmoothingIterations, SmoothingStrength, bUseParallel, ClothCollection, InpainMaskWeightMapName, TransferBoneWeights);
			}
			else if (TransferMethod == EChaosClothAssetTransferSkinWeightsMethod::ClosestPointOnSurface)
			{
				bTransferResult = TransferClosestPointOnSurface(TargetRefSkeleton, bUseParallel, ClothFacade, TransferBoneWeights);
			}
			else
			{
				checkNoEntry();
			}

			if (!bTransferResult)
			{
				FClothDataflowTools::LogAndToastWarning(*this,
					LOCTEXT("TransferWeightsFailedHeadline", "Transfer Weights Failed."),
					LOCTEXT("TransferWeightsDetails", "Failed to transfer skinning weights from the source."));
				SetValue(Context, MoveTemp(*ClothCollection), &Collection);
				return;
			}

		}

		SetValue(Context, MoveTemp(*ClothCollection), &Collection);
	}
}

#undef LOCTEXT_NAMESPACE
