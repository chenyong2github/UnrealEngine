// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Dataflow/DataflowNode.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "DeleteRenderMeshNode.generated.h"

/** Delete the current render mesh stored on the cloth collection. */
USTRUCT(Meta = (DataflowCloth))
struct FChaosClothAssetDeleteRenderMeshNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FChaosClothAssetDeleteRenderMeshNode, "DeleteRenderMesh", "Cloth", "Cloth Delete Render Mesh")

public:
	UPROPERTY(Meta = (Dataflowinput, DataflowOutput, DataflowPassthrough = "Collection"))
	FManagedArrayCollection Collection;

	FChaosClothAssetDeleteRenderMeshNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

private:
	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};
