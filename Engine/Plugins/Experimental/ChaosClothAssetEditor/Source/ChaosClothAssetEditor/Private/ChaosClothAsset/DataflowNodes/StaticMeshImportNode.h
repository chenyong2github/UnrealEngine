// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Dataflow/DataflowNode.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "StaticMeshImportNode.generated.h"

class UStaticMesh;

USTRUCT(Meta = (DataflowCloth))
struct FChaosClothAssetStaticMeshImportNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FChaosClothAssetStaticMeshImportNode, "StaticMeshImport", "Cloth", "Cloth Static Mesh Import")
public:

	UPROPERTY(Meta = (DataflowOutput, DisplayName = "Collection"))
	FManagedArrayCollection Collection;

	UPROPERTY(EditAnywhere, Category = "Static Mesh")
	TObjectPtr<const UStaticMesh> StaticMesh;

	UPROPERTY(EditAnywhere, Category = "Static Mesh")
	int32 UVChannel = INDEX_NONE;

	FChaosClothAssetStaticMeshImportNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};
