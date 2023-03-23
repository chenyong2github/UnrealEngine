// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Dataflow/DataflowNode.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "ReverseNormalsNode.generated.h"

USTRUCT(Meta = (DataflowCloth))
struct FChaosClothAssetReverseNormalsNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FChaosClothAssetReverseNormalsNode, "ReverseNormals", "Cloth", "Cloth Reverse Simulation Render Mesh Normals")
	//DATAFLOW_NODE_RENDER_TYPE(FGeometryCollection::StaticType(), "Collection")  // TODO: Leave out the render type until there is something to render

public:

	UPROPERTY(Meta = (Dataflowinput, DataflowOutput, DisplayName = "Collection", DataflowPassthrough = "Collection"))
	FManagedArrayCollection Collection;

	/** List of patterns to apply the operation on. All patterns will be used if left empty. */
	UPROPERTY(EditAnywhere, Category = "Pattern Selection")
	TArray<int32> Patterns;

	/** Whether to reverse the simulation mesh normals. */
	UPROPERTY(EditAnywhere, Category = "Reverse Normals")
	bool bReverseSimMeshNormals = true;

	/** Whether to reverse the render mesh normals. */
	UPROPERTY(EditAnywhere, Category = "Reverse Normals")
	bool bReverseRenderMeshNormals = true;

	FChaosClothAssetReverseNormalsNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};
