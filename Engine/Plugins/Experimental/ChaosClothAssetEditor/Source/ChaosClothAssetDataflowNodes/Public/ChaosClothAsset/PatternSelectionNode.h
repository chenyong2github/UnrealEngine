// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Dataflow/DataflowNode.h"
#include "PatternSelectionNode.generated.h"

USTRUCT(Meta = (DataflowCloth))
struct FChaosClothAssetPatternSelectionNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FChaosClothAssetPatternSelectionNode, "PatternSelection", "Cloth", "Cloth Pattern Selection")
	//DATAFLOW_NODE_RENDER_TYPE(FGeometryCollection::StaticType(), "Collection")  // TODO: Leave out the render type until there is something to render

public:

	UPROPERTY(EditAnywhere, Category = "Pattern", Meta = (DataflowOutput, DisplayName = "Patterns"))
	TArray<int32> Patterns;

	FChaosClothAssetPatternSelectionNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};
