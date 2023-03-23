// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/PatternSelectionNode.h"
#include "ChaosClothAsset/DataflowNodes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PatternSelectionNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetPatternSelectionNode"

FChaosClothAssetPatternSelectionNode::FChaosClothAssetPatternSelectionNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterOutputConnection(&Patterns);
}

void FChaosClothAssetPatternSelectionNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<int32>>(&Patterns))
	{
		// Init with empty pattern indices
		const TArray<int32> Selection;
		SetValue<TArray<int32>>(Context, Selection, &Patterns);
	}
}

#undef LOCTEXT_NAMESPACE
