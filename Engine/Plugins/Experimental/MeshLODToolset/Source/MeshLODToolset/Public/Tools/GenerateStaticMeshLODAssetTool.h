// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "Templates/PimplPtr.h"
#include "GenerateStaticMeshLODAssetTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;
class IAssetGenerationAPI;
class FGenerateStaticMeshLODProcess;



/**
 *
 */
UCLASS()
class MESHLODTOOLSET_API UGenerateStaticMeshLODAssetToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IAssetGenerationAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};


/**
 * Standard properties
 */
UCLASS()
class MESHLODTOOLSET_API UGenerateStaticMeshLODAssetToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = AssetOptions, meta = (TransientToolProperty))
	FString OutputName;

	/** Base name for newly-generated asset */
	UPROPERTY(EditAnywhere, Category = AssetOptions, meta = (TransientToolProperty))
	FString GeneratedSuffix;

	/** Name of asset that will be updated */
	UPROPERTY(VisibleAnywhere, Category = AssetOptions, meta = (TransientToolProperty))
	FString OutputAsset;
};






/**
 * Simple tool to combine multiple meshes into a single mesh asset
 */
UCLASS()
class MESHLODTOOLSET_API UGenerateStaticMeshLODAssetTool : public UMultiSelectionTool
{
	GENERATED_BODY()

public:
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IAssetGenerationAPI* AssetAPI);

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;

protected:

	UPROPERTY()
	UGenerateStaticMeshLODAssetToolProperties* BasicProperties;

protected:
	UWorld* TargetWorld;
	IAssetGenerationAPI* AssetAPI;

	TPimplPtr<FGenerateStaticMeshLODProcess> GenerateProcess;

	void CreateNewAsset();
	void UpdateExistingAsset();
};
