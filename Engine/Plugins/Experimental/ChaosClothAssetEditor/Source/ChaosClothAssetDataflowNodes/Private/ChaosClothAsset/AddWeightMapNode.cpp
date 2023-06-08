// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/AddWeightMapNode.h"
#include "ChaosClothAsset/DataflowNodes.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/ClothGeometryTools.h"

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
		const FString InNameString = GetValue<FString>(Context, &Name);

		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(InCollection));
		FCollectionClothFacade ClothFacade(ClothCollection);
		if (ClothFacade.IsValid())  // Can only act on the collection if it is a valid cloth collection
		{
			const FName InName(InNameString);
			ClothFacade.AddWeightMap(InName);		// Does nothing if weight map already exists

			TArrayView<float> ClothWeights = ClothFacade.GetWeightMap(InName);
			const int32 MaxWeightIndex = FMath::Min(VertexWeights.Num(), ClothWeights.Num());
			if (VertexWeights.Num() > 0 && VertexWeights.Num() != ClothWeights.Num())
			{
				DataflowNodes::LogAndToastWarning(
					FText::Format(LOCTEXT("WeightMapSize", "FChaosClothAssetAddWeightMapNode: Vertex count mismatch: vertex weights in the node: {0}; 3D vertices in cloth: {1}"),
						VertexWeights.Num(),
						ClothWeights.Num()));
			}

			for (int32 VertexID = 0; VertexID < MaxWeightIndex; ++VertexID)
			{
				ClothWeights[VertexID] = VertexWeights[VertexID];
			}
		}
		SetValue<FManagedArrayCollection>(Context, *ClothCollection, &Collection);
		SetValue<FString>(Context, InNameString, &Name);
	}
}

#undef LOCTEXT_NAMESPACE
