// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"

#include "Dataflow/DataflowNodesConnectionTypes.h"
#include "Dataflow/DataflowCore.h"
#include "Dataflow/DataflowEngine.h"


#include "DataflowSkeletalMeshNodes.generated.h"


DEFINE_LOG_CATEGORY_STATIC(LogDataflowSkeletalMeshNodes, Log, All);

class USkeletalMesh;

USTRUCT()
struct DATAFLOWNODES_API FGetSkeletalMeshDataflowNode: public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGetSkeletalMeshDataflowNode, "SkeletalMesh", "Dataflow", "Skeletal Mesh")

public:
	
	UPROPERTY(EditAnywhere, Category = "Dataflow")
	TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;


	UPROPERTY(EditAnywhere, Category = "Dataflow" )
	FName PropertyName = "SkeletalMesh";

	Dataflow::TOutput<Dataflow::USkeletalMeshPtr> SkeletalMeshOut;

	FGetSkeletalMeshDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
		, SkeletalMeshOut({ FName("SkeletalMeshOut"), this })
	{}


	virtual void Evaluate(const Dataflow::FContext& Context, Dataflow::FConnection* Out) const override;
};

USTRUCT()
struct DATAFLOWNODES_API FSkeletalMeshBoneDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FSkeletalMeshBoneDataflowNode, "SkeletalMeshBone", "Dataflow", "Skeletal Mesh")

public:
	UPROPERTY(EditAnywhere, Category = "Dataflow")
	FName BoneName;

	Dataflow::TInput<Dataflow::USkeletalMeshPtr> SkeletalMeshIn;
	Dataflow::TOutput<int> BoneIndexOut;

	FSkeletalMeshBoneDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
		, SkeletalMeshIn({ FName("SkeletalMeshIn"), this })
		, BoneIndexOut({ FName("BoneIndexOut"), this, INDEX_NONE })
	{}


	virtual void Evaluate(const Dataflow::FContext& Context, Dataflow::FConnection* Out) const override;
};

namespace Dataflow
{
	void RegisterSkeletalMeshNodes();
}

