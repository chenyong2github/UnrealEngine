// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/DeleteRenderMeshNode.h"
#include "ChaosClothAsset/DataflowNodes.h"
#include "ChaosClothAsset/ClothGeometryTools.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DeleteRenderMeshNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetDeleteRenderMeshNode"

FChaosClothAssetDeleteRenderMeshNode::FChaosClothAssetDeleteRenderMeshNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterInputConnection(&Patterns);
	RegisterOutputConnection(&Collection, &Collection);
}

void FChaosClothAssetDeleteRenderMeshNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		using namespace UE::Chaos::ClothAsset;

		// Evaluate in collection
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(InCollection));

		if (Patterns.IsEmpty())
		{
			FClothGeometryTools::DeleteRenderMesh(ClothCollection);
		}
		else
		{
			DataflowNodes::LogAndToastWarning(LOCTEXT("DeletePerPatternNotImplemented", "FClothAssetDeleteRenderMeshNode: Delete per patterns not implemented."));
		}

		SetValue<FManagedArrayCollection>(Context, *ClothCollection, &Collection);
	}
}

#undef LOCTEXT_NAMESPACE
