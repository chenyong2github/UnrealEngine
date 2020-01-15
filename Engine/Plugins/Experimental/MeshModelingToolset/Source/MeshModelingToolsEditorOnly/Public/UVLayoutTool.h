// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "MeshOpPreviewHelpers.h"
#include "ToolDataVisualizer.h"
#include "DynamicMesh3.h"
#include "BaseTools/SingleClickTool.h"
#include "Properties/MeshMaterialProperties.h"

#include "UVLayoutTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UUVLayoutToolBuilder : public UInteractiveToolBuilder
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
class MESHMODELINGTOOLSEDITORONLY_API UUVLayoutToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UUVLayoutToolProperties();


// TODO: add config settings for uvlayout (currently nothing really in the actual algorithm is exposed as a usable parameter)

};




/**
 * Advanced properties
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UUVLayoutAdvancedProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UUVLayoutAdvancedProperties();
};


/**
 * Factory with enough info to spawn the background-thread Operator to do a chunk of work for the tool
 *  stores a pointer to the tool and enough info to know which specific operator it should spawn
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UUVLayoutOperatorFactory : public UObject, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	UUVLayoutTool *Tool;

	int ComponentIndex;

};

/**
 * Simple Mesh Normal Updating Tool
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UUVLayoutTool : public UMultiSelectionTool
{
	GENERATED_BODY()

public:

	friend UUVLayoutOperatorFactory;

	UUVLayoutTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World, UInteractiveGizmoManager* GizmoManagerIn);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void Tick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent) override;
#endif

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

protected:

	UPROPERTY()
	UUVLayoutToolProperties* BasicProperties;

	UPROPERTY()
	UUVLayoutAdvancedProperties* AdvancedProperties;

	UPROPERTY()
	UExistingMeshMaterialProperties* MaterialSettings = nullptr;



	UPROPERTY()
	TArray<UMeshOpPreviewWithBackgroundCompute*> Previews;


	UPROPERTY()
	UMaterialInstanceDynamic* CheckerMaterial = nullptr;

protected:
	TArray<TSharedPtr<FDynamicMesh3>> OriginalDynamicMeshes;

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	FViewCameraState CameraState;

	void UpdateNumPreviews();

	void GenerateAsset(const TArray<FDynamicMeshOpResult>& Results);
};
