// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "Changes/TransformChange.h"
#include "FrameTypes.h"
#include "BoxTypes.h"
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

	/** When enabled, click-drag to reposition the Pivot */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bEnableSnapDragging = false;

	/** When Snap-Dragging, align source and target normals */
	UPROPERTY(EditAnywhere, Category = Options, meta = (EditCondition = "bEnableSnapDragging == true"))
	EEditPivotSnapDragRotationMode RotationMode = EEditPivotSnapDragRotationMode::AlignFlipped;
};


USTRUCT()
struct FEditPivotTarget
{
	GENERATED_BODY()

	UPROPERTY()
	UTransformProxy* TransformProxy = nullptr;

	UPROPERTY()
	UTransformGizmo* TransformGizmo = nullptr;
};




UENUM()
enum class EEditPivotToolActions
{
	NoAction,

	Center,
	Bottom,
	Top,
	Left,
	Right,
	Front,
	Back
};



UCLASS()
class MESHMODELINGTOOLS_API UEditPivotToolActionPropertySet : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UEditPivotTool> ParentTool;

	void Initialize(UEditPivotTool* ParentToolIn) { ParentTool = ParentToolIn; }
	void PostAction(EEditPivotToolActions Action);

	/** Use the World-Space Bounding Box of the target object, instead of the Object-space Bounding Box */
	UPROPERTY(EditAnywhere, Category = BoxPositions)
	bool bUseWorldBox = false;

	UFUNCTION(CallInEditor, Category = BoxPositions, meta = (DisplayPriority = 1))
	void Center() { PostAction(EEditPivotToolActions::Center); }

	UFUNCTION(CallInEditor, Category = BoxPositions, meta = (DisplayPriority = 2))
	void Bottom() { PostAction(EEditPivotToolActions::Bottom ); }

	UFUNCTION(CallInEditor, Category = BoxPositions, meta = (DisplayPriority = 2))
	void Top() { PostAction(EEditPivotToolActions::Top); }

	UFUNCTION(CallInEditor, Category = BoxPositions, meta = (DisplayPriority = 3))
	void Left() { PostAction(EEditPivotToolActions::Left); }

	UFUNCTION(CallInEditor, Category = BoxPositions, meta = (DisplayPriority = 3))
	void Right() { PostAction(EEditPivotToolActions::Right); }

	UFUNCTION(CallInEditor, Category = BoxPositions, meta = (DisplayPriority = 4))
	void Front() { PostAction(EEditPivotToolActions::Front); }

	UFUNCTION(CallInEditor, Category = BoxPositions, meta = (DisplayPriority = 4))
	void Back() { PostAction(EEditPivotToolActions::Back); }

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

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;


	// ICLickDragBehaviorTarget interface
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
	virtual void OnTerminateDragSequence() override;

public:
	UPROPERTY()
	UEditPivotToolProperties* TransformProps;

	UPROPERTY()
	UEditPivotToolActionPropertySet* EditPivotActions;

	virtual void RequestAction(EEditPivotToolActions ActionType);

protected:
	UWorld* TargetWorld;
	UInteractiveGizmoManager* GizmoManager;

	TArray<int> MapToFirstOccurrences;

	FTransform3d Transform;
	FAxisAlignedBox3d ObjectBounds;
	FAxisAlignedBox3d WorldBounds;
	void Precompute();

	UPROPERTY()
	TArray<FEditPivotTarget> ActiveGizmos;

	void UpdateSetPivotModes(bool bEnableSetPivot);
	void SetActiveGizmos_Single(bool bLocalRotations);
	void ResetActiveGizmos();

	FFrame3d StartDragFrameWorld;
	FTransform StartDragTransform;
	int ActiveSnapDragIndex = -1;

	EEditPivotToolActions PendingAction;
	virtual void ApplyAction(EEditPivotToolActions ActionType);
	virtual void SetPivotToBoxPoint(EEditPivotToolActions ActionPoint);

	void UpdateAssets(const FFrame3d& NewPivotWorldFrame);
};
