// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "BaseBehaviors//BehaviorTargetInterfaces.h"
#include "Changes/TransformChange.h"
#include "FrameTypes.h"
#include "EditPivotTool.generated.h"

class UBaseAxisTranslationGizmo;
class UAxisAngleGizmo;
class UTransformGizmo;
class UTransformProxy;

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UEditPivotToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};




/** Snap-Drag Source Point */
UENUM()
enum class EEditPivotSnapDragSource : uint8
{
	/** Snap-Drag moves the Clicked Point to the Target Location */
	ClickPoint = 0 UMETA(DisplayName = "Click Point"),

	/** Snap-Drag moves the Gizmo/Pivot to the Target Location */
	Pivot = 1 UMETA(DisplayName = "Pivot"),


	LastValue UMETA(Hidden)

};



/** Snap-Drag Rotation Mode */
UENUM()
enum class EEditPivotSnapDragRotationMode : uint8
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
class MESHMODELINGTOOLS_API UEditPivotToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	/** Click-drag starting on the target objects to reposition them on the rest of the scene */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bEnableSnapDragging = false;


	UPROPERTY(EditAnywhere, Category = Options, meta = (EditCondition = "bEnableSnapDragging == true"))
	EEditPivotSnapDragSource SnapDragSource = EEditPivotSnapDragSource::ClickPoint;

	/** When Snap-Dragging, align source and target normals */
	UPROPERTY(EditAnywhere, Category = Options, meta = (EditCondition = "bEnableSnapDragging == true"))
	EEditPivotSnapDragRotationMode RotationMode = EEditPivotSnapDragRotationMode::AlignFlipped;
};


USTRUCT()
struct FEditPivotTarget
{
	GENERATED_BODY()

	UPROPERTY()
	UTransformProxy* TransformProxy;

	UPROPERTY()
	UTransformGizmo* TransformGizmo;
};


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UEditPivotTool : public UMultiSelectionTool, public IClickDragBehaviorTarget
{
	GENERATED_BODY()

public:
	UEditPivotTool();

	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

	virtual void SetWorld(UWorld* World, UInteractiveGizmoManager* GizmoManager);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Tick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override { return true; }

	virtual void OnPropertyModified(UObject* PropertySet, UProperty* Property) override;


	// ICLickDragBehaviorTarget interface
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
	virtual void OnTerminateDragSequence() override;

protected:
	UPROPERTY()
	UEditPivotToolProperties* TransformProps;

protected:
	UWorld* TargetWorld;
	UInteractiveGizmoManager* GizmoManager;

	UPROPERTY()
	TArray<FEditPivotTarget> ActiveGizmos;

	void UpdateSetPivotModes(bool bEnableSetPivot);
	void SetActiveGizmos_Single(bool bLocalRotations);
	void ResetActiveGizmos();

	FFrame3d StartDragFrameWorld;
	FTransform StartDragTransform;
	int ActiveSnapDragIndex = -1;

	void UpdateAssets(const FFrame3d& NewPivotWorldFrame);
};
