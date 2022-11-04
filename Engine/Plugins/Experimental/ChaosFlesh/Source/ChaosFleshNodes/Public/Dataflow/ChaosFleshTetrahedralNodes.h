// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowEngine.h"
#include "GeometryCollection/ManagedArrayCollection.h"

#include "ChaosFleshTetrahedralNodes.generated.h"

class UStaticMesh;


USTRUCT()
struct FGenerateTetrahedralCollectionDataflowNodes : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGenerateTetrahedralCollectionDataflowNodes, "GenerateTetrahedralCollection", "Flesh", "")
	DATAFLOW_NODE_RENDER_TYPE(FGeometryCollection::StaticType(), "Collection")

public:
	typedef FManagedArrayCollection DataType;

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	int32 NumCells = 32;

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	double OffsetPercent = 0.05;

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	TObjectPtr<const UStaticMesh> StaticMesh = nullptr;

	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "Collection"))
	FManagedArrayCollection Collection;

	FGenerateTetrahedralCollectionDataflowNodes(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&StaticMesh);
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

namespace Dataflow
{
	void ChaosFleshTetrahedralNodes();


}





