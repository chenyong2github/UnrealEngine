// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SkeletalMeshImportNode.h"
#include "ChaosClothAsset/DataflowNodes.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/ClothDataflowTools.h"
#include "Animation/Skeleton.h"
#include "BoneWeights.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Rendering/SkeletalMeshModel.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SkeletalMeshImportNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetSkeletalMeshImportNode"

FChaosClothAssetSkeletalMeshImportNode::FChaosClothAssetSkeletalMeshImportNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterOutputConnection(&Collection);
}

void FChaosClothAssetSkeletalMeshImportNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	using namespace UE::Chaos::ClothAsset;
	using namespace UE::Chaos::ClothAsset::DataflowNodes;

	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>();
		if (SkeletalMesh)
		{
			const FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();
			const bool bIsValidLOD = ImportedModel && ImportedModel->LODModels.IsValidIndex(LODIndex);
			if (!ensureAlways(bIsValidLOD))
			{
				SetValue(Context, MoveTemp(*ClothCollection), &Collection);
				return;
			}
			const FSkeletalMeshLODModel &LODModel = ImportedModel->LODModels[LODIndex];

			const bool bIsValidSection = LODModel.Sections.IsValidIndex(SectionIndex);;
			if (!ensureAlways(bIsValidSection))
			{
				SetValue(Context, MoveTemp(*ClothCollection), &Collection);
				return;
			}

			const FSkelMeshSection &Section = LODModel.Sections[SectionIndex];
			FCollectionClothFacade ClothFacade(ClothCollection);
			ClothFacade.DefineSchema();

			if (bImportSimMesh)
			{
				FClothDataflowTools::AddSimPatternsFromSkeletalMeshSection(ClothCollection, LODModel, SectionIndex, UVChannel);
			}

			if (bImportRenderMesh)
			{
				const TArray<FSkeletalMaterial>& Materials = SkeletalMesh->GetMaterials();
				check(SectionIndex < Materials.Num());
				const FString RenderMaterialPathName = Materials[SectionIndex].MaterialInterface ? Materials[SectionIndex].MaterialInterface->GetPathName() : "";
				FClothDataflowTools::AddRenderPatternFromSkeletalMeshSection(ClothCollection, LODModel, SectionIndex, RenderMaterialPathName);
			}

			if (const UPhysicsAsset* PhysicsAsset = SkeletalMesh->GetPhysicsAsset())
			{
				ClothFacade.SetPhysicsAssetPathName(PhysicsAsset->GetPathName());
			}
			if (const USkeleton* Skeleton = SkeletalMesh->GetSkeleton())
			{
				ClothFacade.SetSkeletonAssetPathName(Skeleton->GetPathName());
			}
		}
		SetValue(Context, MoveTemp(*ClothCollection), &Collection);
	}
}

#undef LOCTEXT_NAMESPACE
