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

	/* The Static Mesh to import from */
	UPROPERTY(EditAnywhere, Category = "Static Mesh Import")
	TObjectPtr<const UStaticMesh> StaticMesh;

	/* Which static mesh Lod to import.*/
	UPROPERTY(EditAnywhere, Category = "Static Mesh Import")
	int32 LodIndex = 0;

	/* Import static mesh data as a simulation mesh data.*/
	UPROPERTY(EditAnywhere, Category = "Static Mesh Import")
	bool bImportAsSimMesh = true;

	/* Import static mesh data as render mesh data.*/
	UPROPERTY(EditAnywhere, Category = "Static Mesh Import")
	bool bImportAsRenderMesh = true;

	/* UV Channel used to populate Sim Mesh positions */
	UPROPERTY(EditAnywhere, Category = "Static Mesh Import", meta = (EditCondition = "bImportAsSimMesh"))
	int32 UVChannel = 0;

	/* Apply this scale to the UVs when populating Sim Mesh positions. */
	UPROPERTY(EditAnywhere, Category = "Static Mesh Import", Meta = (AllowPreserveRatio, EditCondition = "bImportAsSimMesh && UVChannel != INDEX_NONE"))
	FVector2f UVScale = { 1.f, 1.f };

	FChaosClothAssetStaticMeshImportNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};
