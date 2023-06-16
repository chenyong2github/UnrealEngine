// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ImportNode.h"
#include "ChaosClothAsset/DataflowNodes.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Materials/Material.h"
#include "PhysicsEngine/PhysicsAsset.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ImportNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetImportNode"

FChaosClothAssetImportNode::FChaosClothAssetImportNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&ClothAsset);
	RegisterInputConnection(&ImportLod);
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

		// Create a new cloth collection with its LOD 0
		const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>();
		FCollectionClothFacade ClothFacade(ClothCollection);
		ClothFacade.DefineSchema();

		// Copy the cloth asset to this node's output collection
		if (bSetValue)
		{
			const TObjectPtr<const UChaosClothAsset>& InClothAsset = GetValue<TObjectPtr<const UChaosClothAsset>>(Context, &ClothAsset);
			const int32& InImportLod = GetValue<int32>(Context, &ImportLod);
			const TArray<TSharedRef<const FManagedArrayCollection>>& InClothCollections = InClothAsset->GetClothCollections();
			if (InImportLod >= 0 && InClothCollections.Num() > InImportLod)
			{
				const FCollectionClothConstFacade InClothFacade(InClothCollections[InImportLod]);
				ClothFacade.Initialize(InClothFacade);
			}
		}
		SetValue(Context, MoveTemp(*ClothCollection), &Collection);
}
}

#undef LOCTEXT_NAMESPACE
