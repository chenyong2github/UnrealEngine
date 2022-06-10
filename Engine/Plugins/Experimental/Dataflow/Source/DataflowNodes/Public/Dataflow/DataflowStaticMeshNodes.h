// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"


#include "Dataflow/DataflowNodesConnectionTypes.h"
#include "Dataflow/DataflowCore.h"
#include "Dataflow/DataflowEngine.h"


#include "DataflowStaticMeshNodes.generated.h"


DEFINE_LOG_CATEGORY_STATIC(LogDataflowStaticMeshNodes, Log, All);

class UStaticMesh;

USTRUCT()
struct DATAFLOWNODES_API FGetStaticMeshDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
		DATAFLOW_NODE_DEFINE_INTERNAL(FGetStaticMeshDataflowNode, "StaticMesh", "Dataflow", "Static Mesh")

public:

	UPROPERTY(EditAnywhere, Category = "Dataflow")
		TObjectPtr<UStaticMesh> StaticMesh = nullptr;


	UPROPERTY(EditAnywhere, Category = "Dataflow")
		FName PropertyName = "StaticMesh";

	TSharedPtr< class Dataflow::TOutput<Dataflow::UStaticMeshPtr> > StaticMeshOut;

	FGetStaticMeshDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
		, StaticMeshOut(new Dataflow::TOutput<Dataflow::UStaticMeshPtr>(Dataflow::TOutputParameters<Dataflow::UStaticMeshPtr>({ FName("StaticMeshOut"), this })))
	{}


	virtual void Evaluate(const Dataflow::FContext& Context, Dataflow::FConnection* Out) const override;
};


namespace Dataflow
{
	void RegisterStaticMeshNodes();
}

