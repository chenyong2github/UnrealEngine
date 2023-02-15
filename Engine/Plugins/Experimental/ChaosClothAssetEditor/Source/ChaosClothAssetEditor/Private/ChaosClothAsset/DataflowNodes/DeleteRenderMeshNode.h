// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Dataflow/DataflowNode.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "DeleteRenderMeshNode.generated.h"

USTRUCT(Meta = (DataflowCloth))
struct FChaosClothAssetDeleteRenderMeshNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FChaosClothAssetDeleteRenderMeshNode, "DeleteRenderMesh", "Cloth", "Cloth Delete Render Mesh")
	//DATAFLOW_NODE_RENDER_TYPE(FGeometryCollection::StaticType(), "Collection")  // TODO: Leave out the render type until there is something to render

public:

	UPROPERTY(Meta = (Dataflowinput, DataflowOutput, DisplayName = "Collection", DataflowPassthrough = "Collection"))
	FManagedArrayCollection Collection;

	/** List of patterns to apply the operation on. All patterns will be used if left empty. */
	UPROPERTY(EditAnywhere, Category = "Pattern Selection")
	TArray<int32> Patterns;

	FChaosClothAssetDeleteRenderMeshNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};
