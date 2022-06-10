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

	TSharedPtr< class Dataflow::TOutput<Dataflow::USkeletalMeshPtr> > SkeletalMeshOut;

	FGetSkeletalMeshDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
		, SkeletalMeshOut(new Dataflow::TOutput<Dataflow::USkeletalMeshPtr>(Dataflow::TOutputParameters<Dataflow::USkeletalMeshPtr>({ FName("SkeletalMeshOut"), this })))
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

	TSharedPtr< class Dataflow::TInput<Dataflow::USkeletalMeshPtr> > SkeletalMeshIn;
	TSharedPtr< class Dataflow::TOutput<int> > BoneIndexOut;

	FSkeletalMeshBoneDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
		, SkeletalMeshIn(new Dataflow::TInput<Dataflow::USkeletalMeshPtr>(Dataflow::TInputParameters<Dataflow::USkeletalMeshPtr>({ FName("SkeletalMeshIn"), this })))
		, BoneIndexOut(new Dataflow::TOutput<int>(Dataflow::TOutputParameters<int>({ FName("BoneIndexOut"), this, INDEX_NONE })))
	{}


	virtual void Evaluate(const Dataflow::FContext& Context, Dataflow::FConnection* Out) const override;
};

namespace Dataflow
{
	void RegisterSkeletalMeshNodes();
}

