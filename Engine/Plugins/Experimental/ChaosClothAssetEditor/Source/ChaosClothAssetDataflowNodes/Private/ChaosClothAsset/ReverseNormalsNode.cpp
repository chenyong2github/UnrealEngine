// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ReverseNormalsNode.h"
#include "ChaosClothAsset/DataflowNodes.h"
#include "ChaosClothAsset/ClothGeometryTools.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ReverseNormalsNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetReverseNormalsNode"

FChaosClothAssetReverseNormalsNode::FChaosClothAssetReverseNormalsNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterInputConnection(&SimPatterns);
	RegisterInputConnection(&RenderPatterns);
	RegisterOutputConnection(&Collection, &Collection);
}

void FChaosClothAssetReverseNormalsNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		using namespace UE::Chaos::ClothAsset;

		// Evaluate in collection
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(InCollection));

		FClothGeometryTools::ReverseMesh(
			ClothCollection,
			bReverseSimMeshNormals,
			bReverseSimMeshWindingOrder,
			bReverseRenderMeshNormals,
			bReverseRenderMeshWindingOrder,
			GetValue<TArray<int32>>(Context, &SimPatterns),
			GetValue<TArray<int32>>(Context, &RenderPatterns));

		SetValue<FManagedArrayCollection>(Context, *ClothCollection, &Collection);
	}
}

#undef LOCTEXT_NAMESPACE
