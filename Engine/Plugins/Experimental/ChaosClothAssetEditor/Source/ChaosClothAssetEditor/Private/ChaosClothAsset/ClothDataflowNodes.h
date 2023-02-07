// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Dataflow/DataflowCore.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "ClothDataflowNodes.generated.h"


USTRUCT(meta = (DataflowCloth))
struct FClothAssetTerminalDataflowNode : public FDataflowTerminalNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FClothAssetTerminalDataflowNode, "ClothAssetTerminal", "Cloth", "Cloth Terminal")
	//DATAFLOW_NODE_RENDER_TYPE(FGeometryCollection::StaticType(), "Collection")

public:

	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "Collection", DataflowPassthrough = "Collection"))
	FManagedArrayCollection Collection;

	FClothAssetTerminalDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void SetAssetValue(TObjectPtr<UObject> OutAsset, Dataflow::FContext& Context) const override;

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};


USTRUCT(meta = (DataflowFlesh))
struct FClothAssetDatasmithImportNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FClothAssetDatasmithImportNode, "Import", "Cloth", "Cloth Datasmith Import")
	//DATAFLOW_NODE_RENDER_TYPE(FGeometryCollection::StaticType(), "Collection")

public:
	
	// Datasmith file to read from
	UPROPERTY(EditAnywhere, Category = "Datasmith Importer")
	FFilePath DatasmithFile;

	// Package to import into
	UPROPERTY(EditAnywhere, Category = "Datasmith Importer")
	FString DestPackageName;

	// ManagedArrayCollection for the first ClothAsset found in the input Datasmith file
	UPROPERTY(meta = (DataflowOutput, DisplayName = "Collection"))
	FManagedArrayCollection Collection;

	FClothAssetDatasmithImportNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

private:

	bool EvaluateImpl(Dataflow::FContext& Context, FManagedArrayCollection& OutCollection) const;

};


namespace Dataflow
{
	void RegisterClothDataflowNodes();
}

