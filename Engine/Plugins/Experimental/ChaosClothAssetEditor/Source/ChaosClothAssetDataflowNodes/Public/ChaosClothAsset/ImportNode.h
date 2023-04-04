// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Dataflow/DataflowNode.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "ImportNode.generated.h"

class UChaosClothAsset;

USTRUCT(Meta = (DataflowCloth))
struct FChaosClothAssetImportNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FChaosClothAssetImportNode, "ClothAssetImport", "Cloth", "Cloth Asset Import")
	//DATAFLOW_NODE_RENDER_TYPE(FGeometryCollection::StaticType(), "Collection")  // TODO: Leave out the render type until there is something to render

public:

	UPROPERTY(Meta = (DataflowOutput, DisplayName = "Collection"))
	FManagedArrayCollection Collection;

	/** The Cloth Asset to import into a collection. */
	UPROPERTY(EditAnywhere, Category = "ClothAsset")
	TObjectPtr<const UChaosClothAsset> ClothAsset;

	/** The LOD to import into the collection. Only one LOD can be imported at a time. */
	UPROPERTY(EditAnywhere, Category = "ClothAsset", Meta = (DisplayName = "Import LOD"))
	int32 ImportLod = 0;

	FChaosClothAssetImportNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};
