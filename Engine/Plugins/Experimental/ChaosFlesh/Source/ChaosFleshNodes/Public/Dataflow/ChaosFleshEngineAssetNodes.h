// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowCore.h"
#include "Chaos/Math/Poisson.h"
#include "ChaosLog.h"
#include "ChaosFlesh/TetrahedralCollection.h"
#include "Dataflow/DataflowEngine.h"

#include "ChaosFleshEngineAssetNodes.generated.h"

USTRUCT()
struct FGetFleshAssetDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGetFleshAssetDataflowNode, "GetFleshAsset", "Flesh", "")
	DATAFLOW_NODE_RENDER_TYPE(FGeometryCollection::StaticType(), "Output")

public:

	UPROPERTY(meta = (DataflowOutput, DisplayName = "Collection"))
	FManagedArrayCollection Output;

	FGetFleshAssetDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterOutputConnection(&Output);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};




USTRUCT()
struct FExampleFleshEditDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FExampleFleshEditDataflowNode, "ExampleFleshEdit", "Flesh", "")
	DATAFLOW_NODE_RENDER_TYPE(FGeometryCollection::StaticType(), "Collection")

public:

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	float Scale = 10.0;

	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "Collection", Passthrough = "Collection"))
	FManagedArrayCollection Collection;

	FExampleFleshEditDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};


USTRUCT()
struct FSetFleshAssetDataflowNode : public FDataflowTerminalNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FSetFleshAssetDataflowNode, "SetFleshAsset", "Flesh", "")

public:

	UPROPERTY(meta = (DataflowInput, DisplayName = "Collection"))
	FManagedArrayCollection Input;

	FSetFleshAssetDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
	: FDataflowTerminalNode(InParam, InGuid)
	{
		RegisterInputConnection(&Input);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};



USTRUCT()
struct FImportFleshDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FImportFleshDataflowNode, "ImportFlesh", "Flesh", "")
	DATAFLOW_NODE_RENDER_TYPE(FGeometryCollection::StaticType(), "Collection")

public:

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	FFilePath Filename;

	UPROPERTY(meta = (DataflowOutput, DisplayName = "Collection"))
	FManagedArrayCollection Collection;
	
	FImportFleshDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterOutputConnection(&Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};


USTRUCT()
struct FConstructTetGridNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FConstructTetGridNode, "TetGrid", "Flesh", "")
	DATAFLOW_NODE_RENDER_TYPE(FGeometryCollection::StaticType(), "Collection")

public:

	UPROPERTY(meta = (DataflowOutput, DisplayName = "Collection"))
	FManagedArrayCollection Collection;

	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (ClampMin = "1"))
	FIntVector GridCellCount = FIntVector(10, 10, 10);

	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (ClampMin = "0.0"))
	FVector GridDomain = FVector(1.0, 1.0, 1.0);

	FConstructTetGridNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterOutputConnection(&Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};



USTRUCT()
struct FComputeFleshMassNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FComputeFleshMassNode, "ComputeFleshMass", "Flesh", "")
	DATAFLOW_NODE_RENDER_TYPE(FGeometryCollection::StaticType(), "Collection")

public:

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	float Density = 1.f;

	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "Collection", Passthrough = "Collection"))
	FManagedArrayCollection Collection;

	FComputeFleshMassNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

/**
* Computes a muscle fiber direction per tetrahedron from a GeometryCollection containing tetrahedra, 
* vertices, and origin & insertion vertex fields.  Fiber directions should smoothly follow the geometry
* oriented from the origin vertices pointing to the insertion vertices.
*/
USTRUCT()
struct FComputeFiberFieldNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FComputeFiberFieldNode, "ComputeFiberField", "Flesh", "")
	DATAFLOW_NODE_RENDER_TYPE(FGeometryCollection::StaticType(), "Collection")

public:
	//typedef FManagedArrayCollection DataType;

	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "Collection", Passthrough = "Collection"))
	FManagedArrayCollection Collection;

	//UPROPERTY(meta = (DataflowInput, DisplayName = "OriginVertexIndices"))
	//TArray<int32> OriginIndices;

	//UPROPERTY(meta = (DataflowInput, DisplayName = "InsertionVertexIndices"))
	//TArray<int32> InsertionIndices;

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	FString OriginInsertionGroupName = FString();

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	FString OriginVertexFieldName = FString("Origin");

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	FString InsertionVertexFieldName = FString("Insertion");

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	int32 MaxIterations = 100;

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	float Tolerance = 1.0e-7;

	FComputeFiberFieldNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

	TArray<int32> GetNonZeroIndices(const TArray<uint8>& Map) const;

	TArray<FVector3f> ComputeFiberField(
		const TManagedArray<FIntVector4>& Elements,
		const TManagedArray<FVector3f>& Vertex,
		const TManagedArray<TArray<int32>>& IncidentElements,
		const TManagedArray<TArray<int32>>& IncidentElementsLocalIndex,
		const TArray<int32>& Origin,
		const TArray<int32>& Insertion) const;
};

namespace Dataflow
{
	void RegisterChaosFleshEngineAssetNodes();
}

