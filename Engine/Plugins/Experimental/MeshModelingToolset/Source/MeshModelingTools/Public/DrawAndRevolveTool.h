// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "ModelingOperators.h" //IDynamicMeshOperatorFactory
#include "InteractiveTool.h" //UInteractiveToolPropertySet
#include "InteractiveToolBuilder.h" //UInteractiveToolBuilder
#include "InteractiveToolChange.h" //FToolCommandChange
#include "MeshOpPreviewHelpers.h" //FDynamicMeshOpResult
#include "Properties/MeshMaterialProperties.h"
#include "Properties/RevolveProperties.h"

#include "DrawAndRevolveTool.generated.h"

class UCollectSurfacePathMechanic;
class UConstructionPlaneMechanic;
class FCurveSweepOp;

UCLASS()
class MESHMODELINGTOOLS_API UDrawAndRevolveToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};


UCLASS()
class MESHMODELINGTOOLS_API URevolveToolProperties : public URevolveProperties
{
	GENERATED_BODY()

public:

	/** Connect the ends of an open profile to the axis to close the top and bottom of the revolved result. Not relevant if profile curve is closed. */
	UPROPERTY(EditAnywhere, Category = RevolveSettings, AdvancedDisplay)
	bool bConnectOpenProfileToAxis = true;

	/** Determines the draw plane and the rotation axis (X in the plane). Can only be edited until the first point is added. */
	UPROPERTY(EditAnywhere, Category = DrawPlane, meta = (EditCondition = "AllowedToEditDrawPlane != 0"))
	FTransform DrawPlaneAndAxis = FTransform(FRotator(90, 0, 0));
	
	/** Determines whether plane control widget snaps to world grid (only relevant if world coordinate mode is active in viewport) .*/
	UPROPERTY(EditAnywhere, Category = DrawPlane)
	bool bSnapToWorldGrid = false;

	// Not user visible- used to disallow draw plane modification.
	UPROPERTY()
	int AllowedToEditDrawPlane = 1; // Using an int instead of a bool because the editor adds a user-editable checkbox otherwise
};

UCLASS()
class MESHMODELINGTOOLS_API URevolveOperatorFactory : public UObject, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	UDrawAndRevolveTool* RevolveTool;
};


/** Draws a profile curve and revolves it around an axis. */
UCLASS()
class MESHMODELINGTOOLS_API UDrawAndRevolveTool : public UInteractiveTool, public IClickBehaviorTarget, public IHoverBehaviorTarget
{
	GENERATED_BODY()

public:
	virtual void SetWorld(UWorld* World) { TargetWorld = World; }
	virtual void SetAssetAPI(IToolsContextAssetAPI* NewAssetApi) { AssetAPI = NewAssetApi; }

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	// IClickBehaviorTarget API
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

	// IHoverBehaviorTarget API
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override {} //do nothing
	virtual void OnEndHover() override {} // do nothing

protected:

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	FViewCameraState CameraState;

	// This information is replicated in the user-editable transform in the settings and in the PlaneMechanic
	// plane, but the tool turned out to be much easier to write and edit with this decoupling.
	FVector3d RevolutionAxisOrigin;
	FVector3d RevolutionAxisDirection;

	bool bProfileCurveComplete = false;

	void UpdateRevolutionAxis(const FTransform& PlaneTransform);

	void UndoCurrentOperation();

	UPROPERTY()
	UCollectSurfacePathMechanic* DrawProfileCurveMechanic = nullptr;

	UPROPERTY()
	UConstructionPlaneMechanic* PlaneMechanic = nullptr;

	UPROPERTY()
	URevolveToolProperties* Settings = nullptr;

	UPROPERTY()
	UNewMeshMaterialProperties* MaterialProperties;

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview = nullptr;

	void StartPreview();

	void GenerateAsset(const FDynamicMeshOpResult& Result);

	friend class FRevolveToolStateChange;
	friend class URevolveOperatorFactory;
};

/** Used to support undo while drawing the profile curve. 
*/
class MESHMODELINGTOOLS_API FRevolveToolStateChange : public FToolCommandChange
{
public:
	bool bHaveDoneUndo = false;

	FRevolveToolStateChange()
	{}

	virtual void Apply(UObject* Object) override {}
	virtual void Revert(UObject* Object) override;
	virtual bool HasExpired(UObject* Object) const override;
	virtual FString ToString() const override;
};