// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Dataflow/DataflowNode.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "BoneWeights.h"
#include "TransferSkinWeightsNode.generated.h"

class USkeletalMesh;

USTRUCT(Meta = (DataflowCloth))
struct FChaosClothAssetTransferSkinWeightsNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FChaosClothAssetTransferSkinWeightsNode, "TransferSkinWeights", "Cloth", "Cloth Transfer Skin Weights")
	//DATAFLOW_NODE_RENDER_TYPE(FGeometryCollection::StaticType(), "Collection")  // TODO: Leave out the render type until there is something to render

public:

	UPROPERTY(Meta = (Dataflowinput, DataflowOutput, DisplayName = "Collection", DataflowPassthrough = "Collection"))
	FManagedArrayCollection Collection;

	UPROPERTY(EditAnywhere, Category = "Source Mesh", Meta = (Dataflowinput, DisplayName = "SkeletalMesh"))
	TObjectPtr<USkeletalMesh> SkeletalMesh;

	UPROPERTY(EditAnywhere, Category = "Source Mesh", Meta = (Dataflowinput, DisplayName = "SkeletalMeshLOD"))
	int32 SkeletalMeshLOD;

	// The relative transform between the SkeletalMesh and ClothCollection
	UPROPERTY(EditAnywhere, Category = "Source Mesh", Meta = (Dataflowinput, DisplayName = "Transform"))
	FTransform Transform;

	FChaosClothAssetTransferSkinWeightsNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};
