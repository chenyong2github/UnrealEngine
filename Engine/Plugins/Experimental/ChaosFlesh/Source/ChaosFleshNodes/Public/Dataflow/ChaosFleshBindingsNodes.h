// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowEngine.h"
#include "GeometryCollection/ManagedArrayCollection.h"

#include "ChaosFleshBindingsNodes.generated.h"

class UStaticMesh;
class USkeletalMesh;

DECLARE_LOG_CATEGORY_EXTERN(LogMeshBindings, Verbose, All);

USTRUCT(meta = (DataflowFlesh))
struct FGenerateBindings : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGenerateBindings, "GenerateBindings", "Flesh", "")
	DATAFLOW_NODE_RENDER_TYPE(FGeometryCollection::StaticType(), "Collection")

public:
	typedef FManagedArrayCollection DataType;

	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "Collection"))
	FManagedArrayCollection Collection;

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	TObjectPtr<const UStaticMesh> StaticMeshIn = nullptr;

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	TObjectPtr<const USkeletalMesh> SkeletalMeshIn = nullptr;

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	uint32 SurfaceProjectionIterations = 10;

	FGenerateBindings(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection);
		RegisterInputConnection(&StaticMeshIn);
		RegisterInputConnection(&SkeletalMeshIn);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

namespace Dataflow
{
	void ChaosFleshBindingsNodes();
}