// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "MeshOpPreviewHelpers.h"
#include "DynamicMesh3.h"
#include "BaseTools/SingleClickTool.h"
#include "BakeTransformTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;





/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UBakeTransformToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};





/**
 * Standard properties
 */
UCLASS()
class MESHMODELINGTOOLS_API UBakeTransformToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UBakeTransformToolProperties();

	/** Recompute all mesh normals */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bRecomputeNormals;
};



/**
 * Simple Mesh Normal Updating Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UBakeTransformTool : public UMultiSelectionTool
{
	GENERATED_BODY()

public:
	UBakeTransformTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void Tick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;

protected:

	UPROPERTY()
		UBakeTransformToolProperties* BasicProperties;

	UPROPERTY()
		TArray<UPreviewMesh*> Previews;

protected:
	TArray<TSharedPtr<FDynamicMesh3>> OriginalDynamicMeshes;

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	void UpdateAssets(const TArray<TUniquePtr<FDynamicMesh3>>& Results);
};
