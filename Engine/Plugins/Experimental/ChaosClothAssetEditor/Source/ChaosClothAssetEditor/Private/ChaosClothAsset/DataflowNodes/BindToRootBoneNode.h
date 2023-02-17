// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Dataflow/DataflowNode.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "BindToRootBoneNode.generated.h"

USTRUCT(Meta = (DataflowCloth))
struct FChaosClothAssetBindToRootBoneNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FChaosClothAssetBindToRootBoneNode, "BindToRootBone", "Cloth", "Cloth Bind Skinning Weights To Root Bone")
	//DATAFLOW_NODE_RENDER_TYPE(FGeometryCollection::StaticType(), "Collection")  // TODO: Leave out the render type until there is something to render

public:

	UPROPERTY(Meta = (Dataflowinput, DataflowOutput, DisplayName = "Collection", DataflowPassthrough = "Collection"))
	FManagedArrayCollection Collection;

	/** Whether to bind the simulation mesh. */
	UPROPERTY(EditAnywhere, Category = "Bind To Root Bone")
	bool bBindSimMesh = true;

	/** Whether to bind the render mesh. */
	UPROPERTY(EditAnywhere, Category = "Bind To Root Bone")
	bool bBindRenderMesh = true;

	FChaosClothAssetBindToRootBoneNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};
