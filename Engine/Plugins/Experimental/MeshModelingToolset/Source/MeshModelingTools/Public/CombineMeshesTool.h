// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "MeshOpPreviewHelpers.h"
#include "DynamicMesh3.h"
#include "BaseTools/SingleClickTool.h"
#include "CombineMeshesTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;
class IAssetGenerationAPI;




/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UCombineMeshesToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IAssetGenerationAPI* AssetAPI = nullptr;
	bool bIsDuplicateTool = false;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};



/**
 * Standard properties
 */
UCLASS()
class MESHMODELINGTOOLS_API UCombineMeshesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UCombineMeshesToolProperties();

	/** Delete original actors after creating combined actor */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bDeleteSourceActors = true;

};



/**
 * Simple tool to combine multiple meshes into a single mesh asset
 */
UCLASS()
class MESHMODELINGTOOLS_API UCombineMeshesTool : public UMultiSelectionTool
{
	GENERATED_BODY()

public:
	UCombineMeshesTool();

	virtual void SetDuplicateMode(bool bDuplicateMode);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IAssetGenerationAPI* AssetAPI);

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;

protected:

	UPROPERTY()
	UCombineMeshesToolProperties* BasicProperties;

protected:
	UWorld* TargetWorld;
	IAssetGenerationAPI* AssetAPI;

	bool bDuplicateMode;

	void UpdateAssets();
};
