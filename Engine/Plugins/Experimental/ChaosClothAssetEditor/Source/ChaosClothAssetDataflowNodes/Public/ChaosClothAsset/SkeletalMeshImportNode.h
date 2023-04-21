// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Dataflow/DataflowNode.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "SkeletalMeshImportNode.generated.h"

class USkeletalMesh;

USTRUCT(Meta = (DataflowCloth))
struct FChaosClothAssetSkeletalMeshImportNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FChaosClothAssetSkeletalMeshImportNode, "SkeletalMeshImport", "Cloth", "Cloth Skeletal Mesh Import")
public:

	UPROPERTY(Meta = (DataflowOutput, DisplayName = "Collection"))
	FManagedArrayCollection Collection;

	UPROPERTY(EditAnywhere, Category = "Skeletal Mesh")
	TObjectPtr<const USkeletalMesh> SkeletalMesh;

	UPROPERTY(VisibleAnywhere, Category = "Skeletal Mesh")
	int32 LODIndex = 0;

	UPROPERTY(EditAnywhere, Category = "Skeletal Mesh", Meta = (ClampMin = "0"))
	int32 SectionIndex = 0;

	UPROPERTY(EditAnywhere, Category = "Skeletal Mesh")
	bool bImportSimMesh = true;

	UPROPERTY(EditAnywhere, Category = "Skeletal Mesh")
	bool bImportRenderMesh = true;

	UPROPERTY(EditAnywhere, Category = "Skeletal Mesh")
	int32 UVChannel = INDEX_NONE;

	FChaosClothAssetSkeletalMeshImportNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};
