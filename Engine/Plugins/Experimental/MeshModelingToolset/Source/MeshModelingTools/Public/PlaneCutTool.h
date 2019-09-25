// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "InteractiveGizmo.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "MeshOpPreviewHelpers.h"
#include "DynamicMesh3.h"
#include "BaseTools/SingleClickTool.h"
#include "PlaneCutTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;
class UTransformGizmo;
class UTransformProxy;





/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UPlaneCutToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};





/**
 * Standard properties of the plane cut operation
 */
UCLASS()
class MESHMODELINGTOOLS_API UPlaneCutToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPlaneCutToolProperties();

	/** If true, UVs and Normals are discarded  */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bDiscardAttributes;

	/** If true, both halves of the cut are computed */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bKeepBothHalves;

	/** If keeping both halves, separate the two pieces by this amount */
	UPROPERTY(EditAnywhere, Category = Options, meta = (EditCondition = "bKeepBothHalves == true") )
	float SpacingBetweenHalves;

	/** If true, the cut surface is filled with simple planar hole fill surface(s) */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bFillCutHole;

	UPROPERTY(EditAnywhere, Category = Options)
	bool bShowPreview;

	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay)
	bool bFillSpans;
};




UCLASS()
class MESHMODELINGTOOLS_API UPlaneCutOperatorFactory : public UObject, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	// IDynamicMeshOperatorFactory API
	virtual TSharedPtr<FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	UPlaneCutTool *CutTool;

	UPROPERTY()
	bool bCutBackSide = false;
};

/**
 * Simple Mesh Plane Cutting Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UPlaneCutTool : public USingleSelectionTool
{
	GENERATED_BODY()

public:

	friend UPlaneCutOperatorFactory;

	UPlaneCutTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void Tick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent) override;
#endif

	virtual void OnPropertyModified(UObject* PropertySet, UProperty* Property) override;

protected:

	UPROPERTY()
	UPlaneCutToolProperties* BasicProperties;

	/** Origin of cutting plane */
	UPROPERTY()
	FVector CutPlaneOrigin;

	/** Orientation of cutting plane */
	UPROPERTY()
	FQuat CutPlaneOrientation;

	UPROPERTY()
	TArray<UMeshOpPreviewWithBackgroundCompute*> Previews;


protected:
	TSharedPtr<FDynamicMesh3> OriginalDynamicMesh;

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	FViewCameraState CameraState;

	UPROPERTY()
	UTransformGizmo* PlaneTransformGizmo;

	UPROPERTY()
	UTransformProxy* PlaneTransformProxy;

	void TransformChanged(UTransformProxy* Proxy, FTransform Transform);

	void UpdateNumPreviews();

	IClickBehaviorTarget* SetPointInWorldConnector = nullptr;

	virtual void SetCutPlaneFromWorldPos(const FVector& Position, const FVector& Normal);

	void GenerateAsset(const TArray<TUniquePtr<FDynamicMeshOpResult>>& Results);
};
