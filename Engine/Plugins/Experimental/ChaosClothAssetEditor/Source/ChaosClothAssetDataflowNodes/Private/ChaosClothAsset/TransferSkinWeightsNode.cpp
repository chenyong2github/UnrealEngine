// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/TransferSkinWeightsNode.h"
#include "ChaosClothAsset/DataflowNodes.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "DynamicMesh/MeshTransforms.h"
#include "Operations/TransferBoneWeights.h"
#include "SkeletalMeshAttributes.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TransferSkinWeightsNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetTransferSkinWeightsNode"

namespace UE::ChaosClothAsset::Private
{
	void SkeletalMeshToDynamicMesh(USkeletalMesh* FromSkeletalMeshAsset, int32 SourceLODIdx, FDynamicMesh3& ToDynamicMesh)
	{
		FMeshDescription SourceMesh;

		// Check first if we have bulk data available and non-empty.
		if (FromSkeletalMeshAsset->IsLODImportedDataBuildAvailable(SourceLODIdx) && !FromSkeletalMeshAsset->IsLODImportedDataEmpty(SourceLODIdx))
		{
			FSkeletalMeshImportData SkeletalMeshImportData;
			FromSkeletalMeshAsset->LoadLODImportedData(SourceLODIdx, SkeletalMeshImportData);
			SkeletalMeshImportData.GetMeshDescription(SourceMesh);
		}
		else
		{
			// Fall back on the LOD model directly if no bulk data exists. When we commit
			// the mesh description, we override using the bulk data. This can happen for older
			// skeletal meshes, from UE 4.24 and earlier.
			const FSkeletalMeshModel* SkeletalMeshModel = FromSkeletalMeshAsset->GetImportedModel();
			if (SkeletalMeshModel && SkeletalMeshModel->LODModels.IsValidIndex(SourceLODIdx))
			{
				SkeletalMeshModel->LODModels[SourceLODIdx].GetMeshDescription(SourceMesh, FromSkeletalMeshAsset);
			}
		}

		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(&SourceMesh, ToDynamicMesh);
	};

}


FChaosClothAssetTransferSkinWeightsNode::FChaosClothAssetTransferSkinWeightsNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterOutputConnection(&Collection, &Collection);
	RegisterInputConnection(&SkeletalMesh);
	RegisterInputConnection(&Transform);
	RegisterInputConnection(&SkeletalMeshLOD);
}

void FChaosClothAssetTransferSkinWeightsNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	using namespace UE::Chaos::ClothAsset;
	using namespace UE::AnimationCore;
	using namespace UE::Geometry;

	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		// Evaluate inputs
		FManagedArrayCollection InputCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(InputCollection));
		const TObjectPtr<USkeletalMesh> InputSkeletalMesh = GetValue<TObjectPtr<USkeletalMesh>>(Context, &SkeletalMesh);
		const FTransform InputRelativeTransform = GetValue<FTransform>(Context, &Transform);
		const int32 SourceLODIdx = GetValue<int32>(Context, &SkeletalMeshLOD);

		if (InputSkeletalMesh)
		{
			if (!InputSkeletalMesh->IsValidLODIndex(SourceLODIdx))
			{
				UE::Chaos::ClothAsset::DataflowNodes::LogAndToastWarning(LOCTEXT("Warning_InvalidSkeletalMeshLOD", "TransferSkinWeightsNode: The specified input SkeletalMesh LOD is not valid"));
				return;
			}

			// Convert source Skeletal Mesh to Dynamic Mesh
			FDynamicMesh3 SourceDynamicMesh;
			UE::ChaosClothAsset::Private::SkeletalMeshToDynamicMesh(InputSkeletalMesh, SourceLODIdx, SourceDynamicMesh);
			MeshTransforms::ApplyTransform(SourceDynamicMesh, InputRelativeTransform, true);

			FCollectionClothFacade ClothFacade(ClothCollection);

			const USkeleton* const InputSkeleleton = InputSkeletalMesh->GetSkeleton();

			// Compute bone index mappings
			TMap<FName, FBoneIndexType> TargetBoneToIndex;
			const FReferenceSkeleton& TargetRefSkeleton = InputSkeleleton->GetReferenceSkeleton();
			for (int32 Index = 0; Index < TargetRefSkeleton.GetRawBoneNum(); ++Index)
			{
				TargetBoneToIndex.Add(TargetRefSkeleton.GetRawRefBoneInfo()[Index].Name, Index);
			}

			// Setup bone weight transfer operator
			FTransferBoneWeights TransferBoneWeights(&SourceDynamicMesh, FSkeletalMeshAttributes::DefaultSkinWeightProfileName);
			if (TransferBoneWeights.Validate() != EOperationValidationResult::Ok)
			{
				UE::Chaos::ClothAsset::DataflowNodes::LogAndToastWarning(LOCTEXT("Warning_TransferWeightsFailed", "TransferSkinWeightsNode: Transferring skin weights failed"));
				return;
			}

			// Iterate over the LODs and transfer the bone weights from the source Skeletal mesh to the Cloth asset
			for (int TargetLODIdx = 0; TargetLODIdx < ClothFacade.GetNumLods(); ++TargetLODIdx)
			{
				FCollectionClothLodFacade ClothLodFacade = ClothFacade.GetLod(TargetLODIdx);

				// Cloth collection data arrays we are writing to
				TArrayView<int32> SimNumBoneInfluences = ClothLodFacade.GetSimNumBoneInfluences();
				TArrayView<TArray<int32>> SimBoneIndices = ClothLodFacade.GetSimBoneIndices();
				TArrayView<TArray<float>> SimBoneWeights = ClothLodFacade.GetSimBoneWeights();

				TArrayView<int32> RenderNumBoneInfluences = ClothLodFacade.GetRenderNumBoneInfluences();
				TArrayView<TArray<int32>> RenderBoneIndices = ClothLodFacade.GetRenderBoneIndices();
				TArrayView<TArray<float>> RenderBoneWeights = ClothLodFacade.GetRenderBoneWeights();

				const TArrayView<FVector3f> SimPositions = ClothLodFacade.GetSimRestPosition();

				checkSlow(SimPositions.Num() == SimBoneIndices.Num());

				const int32 NumVert = ClothLodFacade.GetNumSimVertices();
				constexpr bool bUseParallel = true;

				// Iterate over each vertex and write the data from FBoneWeights into cloth collection managed arrays
				ParallelFor(NumVert, [&](int32 VertexID)
				{
					const FVector3d PosD = FVector3d(SimPositions[VertexID]);

					UE::AnimationCore::FBoneWeights BoneWeights;
					TransferBoneWeights.Compute(PosD, FTransformSRT3d::Identity(), BoneWeights, &TargetBoneToIndex);

					const int32 NumBones = BoneWeights.Num();

					SimNumBoneInfluences[VertexID] = NumBones;
					SimBoneIndices[VertexID].SetNum(NumBones);
					SimBoneWeights[VertexID].SetNum(NumBones);

					RenderNumBoneInfluences[VertexID] = NumBones;
					RenderBoneIndices[VertexID].SetNum(NumBones);
					RenderBoneWeights[VertexID].SetNum(NumBones);

					for (int BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
					{
						SimBoneIndices[VertexID][BoneIdx] = BoneWeights[BoneIdx].GetBoneIndex();
						SimBoneWeights[VertexID][BoneIdx] = BoneWeights[BoneIdx].GetWeight();

						RenderBoneIndices[VertexID][BoneIdx] = BoneWeights[BoneIdx].GetBoneIndex();
						RenderBoneWeights[VertexID][BoneIdx] = BoneWeights[BoneIdx].GetWeight();
					}

				}, bUseParallel ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread);


				ClothLodFacade.SetSkeletonAssetPathName(InputSkeleleton->GetPathName());
			}
		}

		SetValue<FManagedArrayCollection>(Context, *ClothCollection, &Collection);
	}
}

#undef LOCTEXT_NAMESPACE
