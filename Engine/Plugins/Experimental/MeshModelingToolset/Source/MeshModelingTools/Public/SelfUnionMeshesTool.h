// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "Drawing/LineSetComponent.h"
#include "MeshOpPreviewHelpers.h"
#include "BaseTools/SingleClickTool.h"

#include "CompositionOps/SelfUnionMeshesOp.h"

#include "SelfUnionMeshesTool.generated.h"

// predeclarations
class FDynamicMesh3;


UCLASS()
class MESHMODELINGTOOLS_API USelfUnionMeshesToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};

/** Choice of behavior on accepting CSG */
UENUM()
enum class ESelfUnionAcceptBehavior : uint8
{
	DeleteOriginals = 0			UMETA(DisplayName = "Delete Inputs"),

	HideOriginals = 1			UMETA(DisplayName = "Hide Inputs"),

	LeaveOriginalsUnchanged = 2	UMETA(DisplayName = "Keep Inputs")
};



/**
 * Standard properties of the self-union operation
 */
UCLASS()
class MESHMODELINGTOOLS_API USelfUnionMeshesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Automatically attempt to fill any holes left by merging (e.g. due to numerical errors) */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bAttemptFixHoles = false;

	/** Show boundary edges created by the union operation -- often due to numerical error */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bShowNewBoundaryEdges = true;

	/** What to do with the source Actors/Components when accepting results of tool.*/
	UPROPERTY(EditAnywhere, Category = ToolOutputOptions)
	ESelfUnionAcceptBehavior OnToolAccept;
};



/**
 * Union of meshes, resolving self intersections
 */
UCLASS()
class MESHMODELINGTOOLS_API USelfUnionMeshesTool : public UMultiSelectionTool, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:

	USelfUnionMeshesTool();

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
	USelfUnionMeshesToolProperties* Properties;

	UPROPERTY()
	ULineSetComponent* DrawnLineSet;

	TSharedPtr<FDynamicMesh3> CombinedSourceMeshes;

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	void SetupPreview();

	void GenerateAsset(const FDynamicMeshOpResult& Result);

	void UpdateVisualization();

	// for visualization of any errors in the currently-previewed merge operation
	TArray<int> CreatedBoundaryEdges;
};
