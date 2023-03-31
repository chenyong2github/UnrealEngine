// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/AddWeightMapNode.h"
#include "ChaosClothAsset/DataflowNodes.h"
#include "ChaosClothAsset/CollectionClothFacade.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AddWeightMapNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetAddWeightMapNode"

FChaosClothAssetAddWeightMapNode::FChaosClothAssetAddWeightMapNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterOutputConnection(&Collection, &Collection);
	RegisterInputConnection(&Name);
	RegisterOutputConnection(&Name, &Name);
}

void FChaosClothAssetAddWeightMapNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	using namespace UE::Chaos::ClothAsset;

	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		// Evaluate in collection
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(InCollection));
		FCollectionClothFacade ClothFacade(ClothCollection);

		const FString InNameString = GetValue<FString>(Context, &Name);

		// Note: If the input collection has no valid LODs, just pass it to the output as is (this can happen if the node is asked to Evaluate before the inputs are connected)

		if (ClothFacade.GetNumLods() > 0)
		{
			const FName InName(InNameString);
			ClothFacade.AddWeightMap(InName);		// Does nothing if weight map already exists

			FCollectionClothLodFacade ClothLod = ClothFacade.GetLod(0);
			TArrayView<float> LodWeights = ClothLod.GetWeightMap(InName);

			const int32 MaxWeightIndex = FMath::Min(VertexWeights.Num(), LodWeights.Num());

			if (VertexWeights.Num() != LodWeights.Num())
			{
				DataflowNodes::LogAndToastWarning(
					FText::Format(LOCTEXT("WeightMapSize", "FChaosClothAssetAddWeightMapNode: Vertex count mismatch: vertex weights in the node: {0}; vertices in the specified LOD: {1}"),
						VertexWeights.Num(),
						LodWeights.Num()));
			}
				
			for (int32 VertexID = 0; VertexID < MaxWeightIndex; ++VertexID)
			{
				LodWeights[VertexID] = VertexWeights[VertexID];
			}
		}

		SetValue<FManagedArrayCollection>(Context, *ClothCollection, &Collection);
		SetValue<FString>(Context, InNameString, &Name);
	}
}

#undef LOCTEXT_NAMESPACE
