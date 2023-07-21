// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Dataflow/DataflowNode.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "SelectionToWeightMapNode.generated.h"

/** Convert an integer index selection to a vertex weight map where the map value is one for vertices in the selection set, and zero otherwise */
USTRUCT(Meta = (DataflowCloth))
struct FChaosClothAssetSelectionToWeightMapNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FChaosClothAssetSelectionToWeightMapNode, "SelectionToWeightMap", "Cloth", "Cloth Selection To Weight Map")

public:

	UPROPERTY(Meta = (Dataflowinput, DataflowOutput, DisplayName = "Collection", DataflowPassthrough = "Collection"))
	FManagedArrayCollection Collection;

	/** The name of the selection to convert and also the name of the weight map attribute that will be added to the collection */
	UPROPERTY(Meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Name"))
	FString Name;

	FChaosClothAssetSelectionToWeightMapNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

private:

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};
