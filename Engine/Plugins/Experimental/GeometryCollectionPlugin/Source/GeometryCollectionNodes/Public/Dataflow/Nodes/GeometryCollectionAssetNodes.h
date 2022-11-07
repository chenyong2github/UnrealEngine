// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowEngine.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollectionObject.h"

#include "GeometryCollectionAssetNodes.generated.h"

class UGeometryCollection;

/**
 * Set the geometry collection asset   
  */
USTRUCT()
struct FSetGeometryCollectionAssetDataflowNode : public FDataflowTerminalNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FSetGeometryCollectionAssetDataflowNode, "SetGeometryCollectionAsset", "GeometryCollection", "")

public:
	FSetGeometryCollectionAssetDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual void Evaluate(Dataflow::FContext& Context) const override;

	/** Attribute collection to use for this asset */ 
	UPROPERTY(meta = (DataflowInput, DisplayName = "Collection"))
	FManagedArrayCollection Collection;

	/** Materials array to use for this asset */
	UPROPERTY(meta = (DataflowInput, DisplayName = "Materials"))
	TArray<TObjectPtr<UMaterial>> Materials;
};

/**
 * Get Current geometry collection asset 
 * Note : Use with caution as this may get replaced in a near future for a more generic getAsset node
 */
USTRUCT()
struct FGetGeometryCollectionAssetDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGetGeometryCollectionAssetDataflowNode, "GetGeometryCollectionAsset", "GeometryCollection", "")

public:
	FGetGeometryCollectionAssetDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

	/** Asset this data flow graph instance is assigned to */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "Asset"))
	TObjectPtr<UGeometryCollection> Asset;
};

/**
 * Get the list of the original mesh information used to create a specific geometryc collection asset
 * each entry contains a mesh, a transform and a list of override materials
 */
USTRUCT()
struct FGetGeometryCollectionSourcesDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGetGeometryCollectionSourcesDataflowNode, "GetGeometryCollectionSources", "GeometryCollection", "")

public:
	FGetGeometryCollectionSourcesDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
	
	/** Asset to get geometry sources from */
	UPROPERTY(meta = (DataflowInput, DisplayName = "Asset"))
	TObjectPtr<UGeometryCollection> Asset;
	
	/** array of geometry sources */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "Sources"))
	TArray<FGeometryCollectionSource> Sources;
};

/**
 * create a geometry collection from a set of geometry sources    
 */
USTRUCT()
struct FCreateGeometryCollectionFromSourcesDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCreateGeometryCollectionFromSourcesDataflowNode, "CreateGeometryCollectionFromSources", "GeometryCollection", "")

public:
	FCreateGeometryCollectionFromSourcesDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
	
	/** array of geometry sources */
	UPROPERTY(meta = (DataflowInput, DisplayName = "Sources"))
	TArray<FGeometryCollectionSource> Sources;

	/** Geometry collection newly created */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "Collection"))
	FManagedArrayCollection Collection;

	/** Geometry collection newly created */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "Materials"))
	TArray<TObjectPtr<UMaterial>> Materials;
};

namespace Dataflow
{
	void GeometryCollectionEngineAssetNodes();
}