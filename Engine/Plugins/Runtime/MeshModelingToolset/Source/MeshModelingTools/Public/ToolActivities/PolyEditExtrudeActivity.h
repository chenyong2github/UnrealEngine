// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "DeformationOps/ExtrudeOp.h" // EPolyEditExtrudeMode
#include "GeometryBase.h"
#include "GroupTopology.h" // FGroupTopologySelection
#include "InteractiveToolActivity.h"
#include "FrameTypes.h"

#include "PolyEditExtrudeActivity.generated.h"

class UPolyEditActivityContext;
class UPolyEditPreviewMesh;
class UPlaneDistanceFromHitMechanic;
PREDECLARE_GEOMETRY(class FDynamicMesh3);

UENUM()
enum class EPolyEditExtrudeDirection
{
	SelectionNormal,
	WorldX,
	WorldY,
	WorldZ,
	LocalX,
	LocalY,
	LocalZ
};

UCLASS()
class MESHMODELINGTOOLS_API UPolyEditExtrudeProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	// Not visible in details panel because we decided that we would use a separate "Push/Pull" button.
	EPolyEditExtrudeMode ExtrudeMode = EPolyEditExtrudeMode::MoveAndStitch;

	/** Which way to move vertices during the extrude */
	UPROPERTY(EditAnywhere, Category = ExtrusionOptions)
	EPolyEditExtrudeDirectionMode DirectionMode = EPolyEditExtrudeDirectionMode::SelectedTriangleNormalsEven;

	/** What axis to measure the extrusion distance along. When the direction mode is Single Direction, also controls the direction. */
	UPROPERTY(EditAnywhere, Category = ExtrusionOptions, AdvancedDisplay)
	EPolyEditExtrudeDirection MeasureDirection = EPolyEditExtrudeDirection::SelectionNormal;

	/** Controls whether extruding an entire open-border patch should create a solid or an open shell */
	UPROPERTY(EditAnywhere, Category = ExtrusionOptions)
	bool bShellsToSolids = true;

	/** 
	 * When extruding regions that touch the mesh border, assign the side groups (groups on the 
	 * stitched side of the extrude) in a way that considers edge colinearity. For instance, when
	 * true, extruding a flat rectangle will give four different groups on its sides rather than
	 * one connected group.
	 */
	UPROPERTY(EditAnywhere, Category = Extrude, AdvancedDisplay)
	bool bUseColinearityForSettingBorderGroups = true;
};

/**
 * 
 */
UCLASS()
class MESHMODELINGTOOLS_API UPolyEditExtrudeActivity : public UInteractiveToolActivity,
	public UE::Geometry::IDynamicMeshOperatorFactory,
	public IClickBehaviorTarget, public IHoverBehaviorTarget

{
	GENERATED_BODY()

public:
	// IInteractiveToolActivity
	virtual void Setup(UInteractiveTool* ParentTool) override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual bool CanStart() const override;
	virtual EToolActivityStartResult Start() override;
	virtual bool IsRunning() const override { return bIsRunning; }
	virtual bool CanAccept() const override;
	virtual EToolActivityEndResult End(EToolShutdownType) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void Tick(float DeltaTime) override;

	// IDynamicMeshOperatorFactory
	virtual TUniquePtr<UE::Geometry::FDynamicMeshOperator> MakeNewOperator() override;

	// IClickBehaviorTarget API
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

	// IHoverBehaviorTarget implementation
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override {}
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override {}

	UPROPERTY()
	TObjectPtr<UPolyEditExtrudeProperties> ExtrudeProperties;

protected:
	FVector3d GetExtrudeDirection() const;
	virtual void BeginExtrude();
	virtual void ApplyExtrude();
	virtual void ReinitializeExtrudeHeightMechanic();
	virtual void EndInternal();

	UPROPERTY()
	TObjectPtr<UPlaneDistanceFromHitMechanic> ExtrudeHeightMechanic = nullptr;

	UPROPERTY()
	TObjectPtr<UPolyEditActivityContext> ActivityContext;

	TSharedPtr<UE::Geometry::FDynamicMesh3> PatchMesh;
	TArray<int32> NewSelectionGids;

	bool bIsRunning = false;

	UE::Geometry::FGroupTopologySelection ActiveSelection;
	UE::Geometry::FFrame3d ActiveSelectionFrameWorld;
	UE::Geometry::FFrame3d ExtrusionFrameWorld;
	float UVScaleFactor = 1.0f;

	bool bRequestedApply = false;
};
