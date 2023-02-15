// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/DataflowNodes/TerminalNode.h"
#include "ChaosClothAsset/DataflowNodes/DataflowNodes.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothAdapter.h"
#include "Animation/Skeleton.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Materials/Material.h"
#include "PhysicsEngine/PhysicsAsset.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TerminalNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetTerminalNode"

namespace UE::Chaos::ClothAsset::DataflowNodes
{
	static const FString DefaultSkeletonPathName = TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh_Skeleton.DefaultSkeletalMesh_Skeleton");
}

FChaosClothAssetTerminalNode::FChaosClothAssetTerminalNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowTerminalNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
}

void FChaosClothAssetTerminalNode::SetAssetValue(TObjectPtr<UObject> Asset, Dataflow::FContext& Context) const
{
	if (UChaosClothAsset* ClothAsset = Cast<UChaosClothAsset>(Asset.Get()))
	{
		using namespace UE::Chaos::ClothAsset;

		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		FClothCollection& ClothCollection = *ClothAsset->GetClothCollection();

		// Replace collection
		ClothCollection.Reset();
		InCollection.CopyTo(&ClothCollection);

		// Set materials
		const int32 NumMaterials = ClothCollection.MaterialPathName.Num();

		TArray<FSkeletalMaterial>& Materials = ClothAsset->GetMaterials();
		Materials.Reset(NumMaterials);

		for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
		{
			if (UMaterial* const Material = LoadObject<UMaterial>(ClothAsset, *ClothCollection.MaterialPathName[MaterialIndex], nullptr, LOAD_None, nullptr))
			{
				Materials.Emplace(Material, true, false, Material->GetFName());
			}
			else
			{
				Materials.Emplace();
			}
		}

		// Set reference skeleton
		const FString SkeletonPathName = (ClothCollection.SkeletonAssetPathName.Num() && !ClothCollection.SkeletonAssetPathName[0].IsEmpty()) ?
			ClothCollection.SkeletonAssetPathName[0] :
			DataflowNodes::DefaultSkeletonPathName;

		if (const USkeleton* const Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPathName, nullptr, LOAD_None, nullptr))
		{
			constexpr bool bRebuildClothSimulationModel = false;  // Avoid rebuilding the asset twice
			ClothAsset->SetReferenceSkeleton(Skeleton->GetReferenceSkeleton(), bRebuildClothSimulationModel);
		}

		// Set physics asset (note: the cloth asset's physics asset is only replaced if a collection path name is found valid)
		if (ClothCollection.PhysicsAssetPathName.Num() && !ClothCollection.PhysicsAssetPathName[0].IsEmpty())
		{
			check(ClothCollection.PhysicsAssetPathName.Num() == 1);  // Can't really deal with more than a single physics asset at the moment
			if (UPhysicsAsset* const PhysicsAsset = LoadObject<UPhysicsAsset>(ClothAsset, *ClothCollection.PhysicsAssetPathName[0], nullptr, LOAD_None, nullptr))
			{
				ClothAsset->SetPhysicsAsset(PhysicsAsset);
			}
		}

		// Check there is at least one empty LOD to avoid crashing the render data
		FClothAdapter ClothAdapter(ClothAsset->GetClothCollection());
		if (ClothAdapter.GetNumLods() < 1)
		{
			ClothAdapter.AddLod();
		}

		// Rebuild the asset static data
		ClothAsset->Build();
	}
}

#undef LOCTEXT_NAMESPACE
