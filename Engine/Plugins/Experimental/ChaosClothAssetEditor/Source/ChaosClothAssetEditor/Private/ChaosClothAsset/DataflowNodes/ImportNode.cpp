// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/DataflowNodes/ImportNode.h"
#include "ChaosClothAsset/DataflowNodes/DataflowNodes.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothCollection.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Materials/Material.h"
#include "PhysicsEngine/PhysicsAsset.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ImportNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetImportNode"

FChaosClothAssetImportNode::FChaosClothAssetImportNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterOutputConnection(&Collection);
}

void FChaosClothAssetImportNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	using namespace UE::Chaos::ClothAsset;
	using namespace UE::Chaos::ClothAsset::DataflowNodes;

	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		bool bSetValue = false;
		if (ClothAsset)
		{
			// Copy the main cloth asset details to this dataflow's owner if any
			if (const Dataflow::FEngineContext* EngineContext = Context.AsType<Dataflow::FEngineContext>())
			{
				if (const UChaosClothAsset* const OwnerClothAsset = Cast<UChaosClothAsset>(EngineContext->Owner))
				{
					if (OwnerClothAsset == ClothAsset)
					{
						// Can't create a loop
						bSetValue = false;
						LogAndToastWarning(LOCTEXT("RecursiveAssetLoop", "FClothAssetNode: The source asset cannot be the terminal asset."));
					}
					else
					{
						bSetValue = true;
					}
				}
			}
			else
			{
				// No terminal asset, this is a stray dataflow and it is safe to set the value without fear of a loop
				bSetValue = true;
			}
		}

		// Copy the cloth asset to this node's output collection
		if (bSetValue)
		{
			// TODO: Needs to make cloth collection a facade to avoid the copy of the cloth collection to a managed array
			TSharedPtr<FClothCollection> ClothCollection = MakeShared<FClothCollection>();

			if (ClothAsset->GetClothCollection())
			{
				const TArray<FName> GroupsToSkip =
				{
					FClothCollection::MaterialsGroup,   // Don't copy materials, and collisions but rebuild the
					FClothCollection::CollisionsGroup   // group from the asset for backward compatibility instead
				};
				ClothAsset->GetClothCollection()->CopyTo(ClothCollection.Get(), GroupsToSkip);
			}

			// Rebuild material list
			const TArray<FSkeletalMaterial>& Materials = ClothAsset->GetMaterials();
			const int32 NumMaterials = Materials.Num();

			ClothCollection->AddElements(NumMaterials, FClothCollection::MaterialsGroup);

			for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
			{
				const FSkeletalMaterial& Material = Materials[MaterialIndex];
				ClothCollection->MaterialPathName[MaterialIndex] = Material.MaterialInterface ?
					Material.MaterialInterface->GetPathName() :
					FString();
			}

			// Overwrite physics asset
			if (const UPhysicsAsset* const PhysicsAsset = ClothAsset->GetPhysicsAsset())
			{
				ClothCollection->AddElements(1, FClothCollection::CollisionsGroup);
				ClothCollection->PhysicsAssetPathName[0] = PhysicsAsset->GetPathName();
			}

			SetValue<FManagedArrayCollection>(Context, *ClothCollection, &Collection);
		}
		else
		{
			// Init with an empty cloth collection
			UE::Chaos::ClothAsset::FClothCollection ClothCollection;
			SetValue<FManagedArrayCollection>(Context, ClothCollection, &Collection);
		}
	}
}

#undef LOCTEXT_NAMESPACE
