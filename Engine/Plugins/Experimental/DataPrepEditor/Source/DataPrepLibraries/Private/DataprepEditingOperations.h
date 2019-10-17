// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DataPrepOperation.h"

#include "DataPrepOperationsLibrary.h"

//
#include "DataprepEditingOperations.generated.h"

class AStaticMeshActor;
class IMeshBuilderModule;
class UStaticMesh;
class UStaticMeshComponent;
class UWorld;

UCLASS(Experimental, Category = ObjectOperation, Meta = (DisplayName="Remove Objects", ToolTip = "Remove any asset or actor to process") )
class UDataprepRemoveObjectsOperation : public UDataprepEditingOperation
{
	GENERATED_BODY()

		//~ Begin UDataprepOperation Interface
public:
	virtual FText GetCategory_Implementation() const override
	{
		return FDataprepOperationCategories::ObjectOperation;
	}

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;
	//~ End UDataprepOperation Interface
};

UCLASS(Experimental, Category = ObjectOperation, Meta = (DisplayName="Merge Actors", ToolTip = "Merge the meshes into a unique mesh with the provided StaticMeshActors") )
class UDataprepMergeActorsOperation : public UDataprepEditingOperation
{
	GENERATED_BODY()

public:
	UDataprepMergeActorsOperation()
		: bDeleteMergedActors(true)
		, bDeleteMergedMeshes(true)
		, MergedMesh(nullptr)
		, MergedActor(nullptr)
	{
	}

	//~ Begin UDataprepOperation Interface
public:
	virtual FText GetCategory_Implementation() const override
	{
		return FDataprepOperationCategories::ObjectOperation;
	}

	/** Settings to use for the merge operation */
	UPROPERTY(EditAnywhere, Category = MergeSettings)
	FString NewActorLabel;

	/** Settings to use for the merge operation */
	UPROPERTY(EditAnywhere, Category = MergeSettings)
	bool bDeleteMergedActors;

	/** Settings to use for the merge operation */
	UPROPERTY(EditAnywhere, Category = MergeSettings)
	bool bDeleteMergedMeshes;

	/** Settings to use for the merge operation */
	UPROPERTY(EditAnywhere, Category = MergeSettings)
	FMeshMergingSettings MergeSettings;

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;
	//~ End UDataprepOperation Interface

	bool MergeStaticMeshActors(UWorld* World, const TArray<UPrimitiveComponent*>& ComponentsToMerge, const FString& RootName, bool bCreateActor = true);

	void PrepareStaticMeshes( TSet<UStaticMesh*> StaticMeshes, IMeshBuilderModule& MeshBuilderModule );

protected:
	FVector MergedMeshWorldLocation;
	UStaticMesh* MergedMesh;
	AStaticMeshActor* MergedActor;
};

UCLASS(Experimental, Category = ObjectOperation, Meta = (DisplayName="Smart Merge - PROTO", ToolTip = "Collapse all actors solely holding more than one static mesh actor") )
class UDataprepSmartMergeOperation : public UDataprepMergeActorsOperation
{
	GENERATED_BODY()

	//~ Begin UDataprepOperation Interface
protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;
	//~ End UDataprepOperation Interface

private:
	void SmartMerge(UWorld* World, const TArray<AActor*>& Actors);
};

UCLASS(Experimental, Category = ObjectOperation, Meta = (DisplayName="Clean World - PROTO", ToolTip = "Remove unused assets, collapse actors with only one child") )
class UDataprepCleanWorldOperation : public UDataprepEditingOperation
{
	GENERATED_BODY()

		//~ Begin UDataprepOperation Interface
public:
	virtual FText GetCategory_Implementation() const override
	{
		return FDataprepOperationCategories::ObjectOperation;
	}

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;
	//~ End UDataprepOperation Interface
};
