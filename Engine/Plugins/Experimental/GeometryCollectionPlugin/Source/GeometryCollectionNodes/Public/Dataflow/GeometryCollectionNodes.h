// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "GeometryCollectionNodeConnectionTypes.h"

#include "CoreMinimal.h"
#include "Dataflow/DataflowEngine.h"

#include "GeometryCollectionNodes.generated.h"

USTRUCT()
struct FGetCollectionAssetDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGetCollectionAssetDataflowNode, "GetCollectionAsset", "GeometryCollection", "")

public:
	typedef Dataflow::FManagedArrayCollectionSharedPtr DataType;

	Dataflow::TOutput<DataType> Output;

	FGetCollectionAssetDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
		, Output({ FName("CollectionOut"), this })
	{}

	virtual void Evaluate(const Dataflow::FContext& Context, Dataflow::FConnection* Out) const override;
};

USTRUCT()
struct FExampleCollectionEditDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FExampleCollectionEditDataflowNode, "ExampleCollectionEdit", "GeometryCollection", "")

public:
	UPROPERTY(EditAnywhere, Category = "Dataflow");
	bool Active = true;

	UPROPERTY(EditAnywhere, Category = "Dataflow");
	float Scale = 1.0;

	typedef Dataflow::FManagedArrayCollectionSharedPtr DataType;
	Dataflow::TInput<DataType> Input;
	Dataflow::TOutput<DataType> Output;

	FExampleCollectionEditDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
		, Input({ FName("CollectionIn"), this })
		, Output({ FName("CollectionOut"), this })
	{}

	virtual void Evaluate(const Dataflow::FContext& Context, Dataflow::FConnection* Out) const override;

};

USTRUCT()
struct FSetCollectionAssetDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FSetCollectionAssetDataflowNode, "SetCollectionAsset", "GeometryCollection", "")

public:
	typedef Dataflow::FManagedArrayCollectionSharedPtr DataType;
	Dataflow::TInput<DataType> Input;

	FSetCollectionAssetDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
		, Input({ FName("CollectionIn"), this })
	{}

	virtual void Evaluate(const Dataflow::FContext& Context, Dataflow::FConnection* Out) const override;

};

USTRUCT()
struct FResetGeometryCollectionDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FResetGeometryCollectionDataflowNode, "ResetGeometryCollection", "GeometryCollection", "")

public:
	typedef Dataflow::FManagedArrayCollectionSharedPtr DataType;

	Dataflow::TOutput<DataType> ManagedArrayOut;

	FResetGeometryCollectionDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
		, ManagedArrayOut({ FName("ManagedArrayOut"), this })
	{}


	virtual void Evaluate(const Dataflow::FContext& Context, Dataflow::FConnection* Out) const override;

};

namespace Dataflow
{
	void GeometryCollectionEngineAssetNodes();


}

