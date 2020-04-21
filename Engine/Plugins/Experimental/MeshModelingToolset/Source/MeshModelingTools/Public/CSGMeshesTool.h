// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "Drawing/LineSetComponent.h"
#include "MeshOpPreviewHelpers.h"
#include "BaseTools/SingleClickTool.h"

#include "CompositionOps/BooleanMeshesOp.h"

#include "CSGMeshesTool.generated.h"

// predeclarations
class FDynamicMesh3;
class UTransformGizmo;
class UTransformProxy;


UCLASS()
class MESHMODELINGTOOLS_API UCSGMeshesToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};



/**
 * Standard properties of the CSG operation
 */
UCLASS()
class MESHMODELINGTOOLS_API UCSGMeshesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** The type of operation */
	UPROPERTY(EditAnywhere, Category = Options)
	ECSGOperation Operation = ECSGOperation::Union;

	/** Remove the source Actors/Components when accepting results of tool.*/
	UPROPERTY(EditAnywhere, Category = Options)
	bool bDeleteInputActors = true;

	/** Show UI to allow changing translation, rotation and scale of input meshes */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bShowTransformUI = true;

	/** Snap the cut plane to the world grid */
	UPROPERTY(EditAnywhere, Category = Snapping, meta = (EditCondition = "bShowTransformUI == true"))
	bool bSnapToWorldGrid = false;

	/** Show boundary edges created by the CSG operation -- often due to numerical error */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bShowNewBoundaryEdges = true;

	/** Automatically attempt to fill any holes left by CSG (e.g. due to numerical errors) */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bAttemptFixHoles = false;
};



/**
 * Simple Mesh Plane Cutting Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UCSGMeshesTool : public UMultiSelectionTool, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:

	UCSGMeshesTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent) override;
#endif

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

protected:

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview;

	UPROPERTY()
	UCSGMeshesToolProperties* CSGProperties;

	UPROPERTY()
	TArray<UTransformProxy*> TransformProxies;

	UPROPERTY()
	TArray<UTransformGizmo*> TransformGizmos;

	TArray<TSharedPtr<FDynamicMesh3>> OriginalDynamicMeshes;

	UPROPERTY()
	ULineSetComponent* DrawnLineSet;

	void TransformChanged(UTransformProxy* Proxy, FTransform Transform);

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	void SetupPreview();
	void SetTransformGizmos();
	void UpdateGizmoVisibility();

	void GenerateAsset(const FDynamicMeshOpResult& Result);

	void UpdateVisualization();

	// for visualization of any errors in the currently-previewed CSG operation
	TArray<int> CreatedBoundaryEdges;
};
