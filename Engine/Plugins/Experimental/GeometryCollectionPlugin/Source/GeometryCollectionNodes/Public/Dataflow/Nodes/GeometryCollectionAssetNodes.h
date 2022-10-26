// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowEngine.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollectionObject.h"

#include "GeometryCollectionAssetNodes.generated.h"

class UGeometryCollection;

/**
 * This node is a final node with no output  
 * it is the only one that can have the right to wroite to the asset 
 */
USTRUCT()
struct FFinalDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FFinalDataflowNode, "FinalDataflowNode", "DataFlow", "")
	
public:
	FFinalDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
	}

protected:
	/** No output evaluation - secific to this type of node */
	virtual void EvaluateFinal(Dataflow::FContext& Context) const { ensure(false); }
	
private:
	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override
	{
		EvaluateFinal(Context);
	};
};


/**
 * Set the geometry collection asset   
 * inputs:
 *	- Collection : collection attributes that compose the asset
 *	- Materials : array of materials for the geometry
 */
USTRUCT()
struct FSetGeometryCollectionAssetDataflowNode : public FFinalDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FSetGeometryCollectionAssetDataflowNode, "SetGeometryCollectionAsset", "GeometryCollection", "")

public:
	FSetGeometryCollectionAssetDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	virtual void EvaluateFinal(Dataflow::FContext& Context) const override;

	/** Attribute collection to reset the asset with */ 
	UPROPERTY(meta = (DataflowInput, DisplayName = "Collection"))
	FManagedArrayCollection Collection;

	/** Materials array to rest the asset with */
	UPROPERTY(meta = (DataflowInput, DisplayName = "Materials"))
	TArray<TObjectPtr<UMaterial>> Materials;
};

/**
 * Get Current asset    
 * outputs:
 *	- GeometrySources : array of geometry source ( Asset, transform and material )
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
 * Get geometry source    
 * outputs:
 *	- GeometrySources : array of geometry source ( Asset, transform and material )
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
 * inputs:
 *	- GeometrySources : array of geometry source ( Asset, transform and material )
 *	outputs:
 *	- Collection : Geometry collection newly created
 *	- Materials : Array of materials used by the geometry collection 
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