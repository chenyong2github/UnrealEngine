// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "BaseBehaviors//BehaviorTargetInterfaces.h"
#include "Changes/TransformChange.h"
#include "FrameTypes.h"
#include "TransformMeshesTool.generated.h"

class UBaseAxisTranslationGizmo;
class UAxisAngleGizmo;
class UTransformGizmo;
class UTransformProxy;

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UTransformMeshesToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};



/** Mesh Transform modes */
UENUM()
enum class ETransformMeshesTransformMode : uint8
{
	/** Single Gizmo for all Objects */
	SharedGizmo = 0 UMETA(DisplayName = "Shared Gizmo"),

	/** Single Gizmo for all Objects, Rotations applied per-Object */
	SharedGizmoLocal = 1 UMETA(DisplayName = "Shared Gizmo (Local)"),

	/** Separate Gizmo for each Object */
	PerObjectGizmo = 2 UMETA(DisplayName = "Multi-Gizmo"),

	LastValue UMETA(Hidden)
};



/** Snap-Drag Source Point */
UENUM()
enum class ETransformMeshesSnapDragSource : uint8
{
	/** Snap-Drag moves the Clicked Point to the Target Location */
	ClickPoint = 0 UMETA(DisplayName = "Click Point"),

	/** Snap-Drag moves the Gizmo/Pivot to the Target Location */
	Pivot = 1 UMETA(DisplayName = "Pivot"),


	LastValue UMETA(Hidden)

};



/** Snap-Drag Rotation Mode */
UENUM()
enum class ETransformMeshesSnapDragRotationMode : uint8
{
	/** Snap-Drag only translates, ignoring Normals */
	Ignore = 0 UMETA(DisplayName = "Ignore"),

	/** Snap-Drag aligns the Source and Target Normals to point in the same direction */
	Align = 1 UMETA(DisplayName = "Align"),

	/** Snap-Drag aligns the Source Normal to the opposite of the Target Normal direction */
	AlignFlipped = 2 UMETA(DisplayName = "Align Flipped"),

	LastValue UMETA(Hidden)
};



/**
 * Standard properties of the Transform Meshes operation
 */
UCLASS()
class MESHMODELINGTOOLS_API UTransformMeshesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category = Options)
	ETransformMeshesTransformMode TransformMode = ETransformMeshesTransformMode::SharedGizmo;


	UPROPERTY(EditAnywhere, Category = Options, meta = (EditCondition = "TransformMode == ETransformMeshesTransformMode::SharedGizmo || TransformMode == ETransformMeshesTransformMode::PerObjectGizmo"))
	bool bSetPivot = false;


	/** Click-drag starting on the target objects to reposition them on the rest of the scene */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bEnableSnapDragging = false;


	UPROPERTY(EditAnywhere, Category = Options, meta = (EditCondition = "bEnableSnapDragging == true"))
	ETransformMeshesSnapDragSource SnapDragSource = ETransformMeshesSnapDragSource::ClickPoint;

	/** When Snap-Dragging, align source and target normals */
	UPROPERTY(EditAnywhere, Category = Options, meta = (EditCondition = "bEnableSnapDragging == true"))
	ETransformMeshesSnapDragRotationMode RotationMode = ETransformMeshesSnapDragRotationMode::AlignFlipped;
};


USTRUCT()
struct FTransformMeshesTarget
{
	GENERATED_BODY()

	UPROPERTY()
	UTransformProxy* TransformProxy = nullptr;

	UPROPERTY()
	UTransformGizmo* TransformGizmo = nullptr;
};


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UTransformMeshesTool : public UMultiSelectionTool, public IClickDragBehaviorTarget
{
	GENERATED_BODY()

public:
	UTransformMeshesTool();

	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

	virtual void SetWorld(UWorld* World, UInteractiveGizmoManager* GizmoManager);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return false; }
	virtual bool HasAccept() const override { return false; }
	virtual bool CanAccept() const override { return false; }

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;


	// ICLickDragBehaviorTarget interface
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
	virtual void OnTerminateDragSequence() override;

protected:
	UPROPERTY()
	UTransformMeshesToolProperties* TransformProps;

protected:
	UWorld* TargetWorld;
	UInteractiveGizmoManager* GizmoManager;

	UPROPERTY()
	TArray<FTransformMeshesTarget> ActiveGizmos;


	ETransformMeshesTransformMode CurTransformMode;
	void UpdateTransformMode(ETransformMeshesTransformMode NewMode);
	bool bCurSetPivotMode;
	void UpdateSetPivotModes(bool bEnableSetPivot);

	void SetActiveGizmos_Single(bool bLocalRotations);
	void SetActiveGizmos_PerObject();
	void ResetActiveGizmos();


	FFrame3d StartDragFrameWorld;
	FTransform StartDragTransform;
	int ActiveSnapDragIndex = -1;

	void OnParametersUpdated();
};
