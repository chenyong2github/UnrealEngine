// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "MeshOpPreviewHelpers.h"
#include "Properties/MeshMaterialProperties.h"

#include "ParameterizeMeshTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;



/** Material modes for MeshInspectorTool */
UENUM()
enum class EParameterizeMeshMaterialMode : uint8
{
	/** Input material */
	Default,

	/** Checkerboard material */
	Checkerboard,

	/** Override material */
	Override
};

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UParameterizeMeshToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:

	IToolsContextAssetAPI* AssetAPI;

	UParameterizeMeshToolBuilder()
	{
		AssetAPI = nullptr;
	}

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};





/**
 * Simple Mesh Simplifying Tool
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UParameterizeMeshTool : public USingleSelectionTool, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	UParameterizeMeshTool();

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Tick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	// IDynamicMeshOperatorFactory API
	virtual TSharedPtr<FDynamicMeshOperator> MakeNewOperator() override;

protected:
	// need to update bResultValid if these are modified, so we don't publicly expose them. 
	// @todo setters/getters for these

	/** Maximum amount of stretch, from none to any.  If zero stretch is specified each triangle will likey be its own chart */
	UPROPERTY(EditAnywhere, Category = Options, meta = (Default = "0.166", UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax= "1"))
	float ChartStretch = 0.11f;

	UPROPERTY()
	UExistingMeshMaterialProperties* MaterialSettings = nullptr;

protected:
	UPROPERTY()
	UMaterialInterface* DefaultMaterial = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* DisplayMaterial = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* CheckerMaterial = nullptr;
	
	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview = nullptr;


protected:
	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	TSharedPtr<FMeshDescription> InputMesh;

};
