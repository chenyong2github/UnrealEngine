// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "BoxTypes.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "FrameTypes.h"
#include "GeometryBase.h"
#include "InteractiveTool.h"
#include "InteractiveToolBuilder.h"
#include "InteractiveToolQueryInterfaces.h" // IInteractiveToolNestedAcceptCancelAPI
#include "Mechanics/CubeGrid.h"
#include "ModelingOperators.h"
#include "OrientedBoxTypes.h"
#include "Spatial/GeometrySet3.h"
#include "ToolContextInterfaces.h" // FViewCameraState
#include "ToolDataVisualizer.h"

#include "CubeGridTool.generated.h"

PREDECLARE_GEOMETRY(class FDynamicMesh3);
PREDECLARE_GEOMETRY(class FDynamicMeshChange);
PREDECLARE_GEOMETRY(class FCubeGrid);

class IAssetGenerationAPI;
class UClickDragInputBehavior;
class UCubeGridTool;
class UCreateMeshObjectTypeProperties;
class UDragAlignmentMechanic;
class ULocalClickDragInputBehavior;
class ULocalSingleClickInputBehavior;
class UMeshOpPreviewWithBackgroundCompute;
class UMouseHoverBehavior;
class UNewMeshMaterialProperties;
class UPreviewGeometry;
class UCombinedTransformGizmo;
class UTransformProxy;
class UToolTarget;


UCLASS()
class MESHMODELINGTOOLSEXP_API UCubeGridToolBuilder : public UInteractiveToolWithToolTargetsBuilder
{
	GENERATED_BODY()

public:
	IAssetGenerationAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

protected:

	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
};


UENUM()
enum class ECubeGridToolFaceSelectionMode
{
	OutsideBasedOnNormal,
	InsideBasedOnNormal,
	OutsideBasedOnViewRay,
	InsideBasedOnViewRay
};


UCLASS()
class MESHMODELINGTOOLSEXP_API UCubeGridToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = Options, meta = (
		EditCondition = "bAllowedToEditGrid", HideEditConditionToggle))
	FVector GridFrameOrigin = FVector(0, 0, 0);

	UPROPERTY(EditAnywhere, Category = Options, meta = (
		EditCondition = "bAllowedToEditGrid", HideEditConditionToggle,
		UIMin = -180, UIMax = 180, ClampMin = -180000, ClampMax = 18000))
	FRotator GridFrameOrientation = FRotator(0, 0, 0);

	UPROPERTY(EditAnywhere, Category = Options, meta = (TransientToolProperty))
	bool bShowGizmo = false;

	/** How many blocks each push/pull invocation will do at a time.*/
	UPROPERTY(EditAnywhere, Category = Options, meta = (
		UIMin = "0", ClampMin = "0"))
	int32 BlocksPerStep = 1;

	/** Determines cube grid scale. Can also be adjusted with Ctrl + E/Q. */
	UPROPERTY(EditAnywhere, Category = Options, meta = (
		EditCondition = "bAllowedToEditGrid", HideEditConditionToggle,
		UIMin = "0", UIMax = "10", ClampMin = "0", ClampMax = "31"))
	uint8 PowerOfTwo = 5;

	// Must match ClampMax in PowerOfTwo, used to make hotkeys not exceed it.
	const uint8 MaxPowerOfTwo = 31;

	/** Smallest block size to use in the grid. For instance, 3.125 results in
	 blocks that are 100 sized at 5 power of two since 3.125 * 2^5 = 100. */
	UPROPERTY(EditAnywhere, Category = Options, meta = (
		EditCondition = "bAllowedToEditGrid", HideEditConditionToggle,
		UIMin = "0.1", UIMax = "10", ClampMin = "0.001", ClampMax = "1000"))
	double BlockBaseSize = 3.125;

	/** When pushing/pulling in a way where the diagonal matters, setting this to true
	 makes the diagonal generally try to lie flat across the face rather than at
	 an incline. */
	UPROPERTY(EditAnywhere, Category = Options, meta = (
		EditCondition = "bInCornerMode", HideEditConditionToggle))
	bool bCrosswiseDiagonal = false;

	/** When performing selection, the tolerance to use when determining
	 whether things lie in the same plane as a cube face. */
	UPROPERTY(EditAnywhere, Category = BlockSelection, AdvancedDisplay, meta = (
		UIMin = "0", UIMax = "0.5", ClampMin = "0", ClampMax = "10"))
	double PlaneTolerance = 0.01;

	/** When raycasting to find a selected grid face, this determines whether geometry
	 in the scene that is not part of the edited mesh is hit. */
	UPROPERTY(EditAnywhere, Category = BlockSelection)
	bool bHitUnrelatedGeometry = true;

	/** When the grid ground plane is above some geometry, whether we should hit that
	 plane or pass through to the other geometry. */
	UPROPERTY(EditAnywhere, Category = BlockSelection, AdvancedDisplay)
	bool bHitGridGroundPlaneIfCloser = false;
	
	/** How the selected face is determined. */
	UPROPERTY(EditAnywhere, Category = BlockSelection, AdvancedDisplay)
	ECubeGridToolFaceSelectionMode FaceSelectionMode = ECubeGridToolFaceSelectionMode::OutsideBasedOnNormal;

	UPROPERTY(VisibleAnywhere, Category = ShortcutInfo)
	FString ToggleCornerMode = TEXT("Z to start/complete corner mode.");

	UPROPERTY(VisibleAnywhere, Category = ShortcutInfo)
	FString PushPull = TEXT("E/Q to pull/push, or use Ctrl+drag.");

	UPROPERTY(VisibleAnywhere, Category = ShortcutInfo)
	FString ResizeGrid = TEXT("Ctrl + E/Q to increase/decrease grid in powers of two.");

	UPROPERTY(VisibleAnywhere, Category = ShortcutInfo)
	FString SlideSelection = TEXT("Middle mouse drag to slide selection in plane. "
		"Shift + E/Q to shift selection back/forward.");

	UPROPERTY(VisibleAnywhere, Category = ShortcutInfo)
	FString FlipSelection = TEXT("T to flip the selection.");

	UPROPERTY(VisibleAnywhere, Category = ShortcutInfo)
	FString GridGizmo = TEXT("R to show/hide grid gizmo.");

	UPROPERTY(VisibleAnywhere, Category = ShortcutInfo)
	FString QuickShiftGizmo = TEXT("Ctrl + middle click to quick-reposition "
		"the gizmo while keeping it on grid.");

	UPROPERTY(VisibleAnywhere, Category = ShortcutInfo)
	FString AlignGizmo = TEXT("While dragging gizmo handles, hold Ctrl to align "
		"to items in scene (constrained to the moved axes).");

	UPROPERTY(meta = (TransientToolProperty))
	bool bInCornerMode = false;

	//~ Currently unused... Used to disallow it during corner mode, might do so again.
	UPROPERTY(meta = (TransientToolProperty))
	bool bAllowedToEditGrid = true;
};

UENUM()
enum class ECubeGridToolAction
{
	NoAction,
	Push,
	Pull,
	Flip,
	SlideForward,
	SlideBack,
	DecreasePowerOfTwo,
	IncreasePowerOfTwo,
	CornerMode,
	// FitGrid,
	Done,
	Cancel,
};

UCLASS()
class MESHMODELINGTOOLSEXP_API UCubeGridToolActions : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UCubeGridTool> ParentTool;

	void Initialize(UCubeGridTool* ParentToolIn) { ParentTool = ParentToolIn; }

	void PostAction(ECubeGridToolAction Action);

	/** Can also be invoked with E. */
	UFUNCTION(CallInEditor, Category = Actions, meta = (DisplayPriority = 1))
	void Pull() { PostAction(ECubeGridToolAction::Pull); }

	/** Can also be invoked with Q. */
	UFUNCTION(CallInEditor, Category = Actions, meta = (DisplayPriority = 2))
	void Push() { PostAction(ECubeGridToolAction::Push); }

	/** Can also be invoked with Shift + E. */
	UFUNCTION(CallInEditor, Category = Actions, meta = (DisplayPriority = 3))
	void SlideBack() { PostAction(ECubeGridToolAction::SlideBack); }

	/** Can also be invoked with Shift + Q. */
	UFUNCTION(CallInEditor, Category = Actions, meta = (DisplayPriority = 4))
	void SlideForward() { PostAction(ECubeGridToolAction::SlideForward); }

	/** Engages a mode where specific corners can be selected to push/pull only
	 those corners. Press Apply to commit the result afterward. Can also be toggled
	 with Z. */
	UFUNCTION(CallInEditor, Category = Actions, meta = (DisplayPriority = 5))
	void CornerMode() { PostAction(ECubeGridToolAction::CornerMode); }

	/** Can also be invoked with T. */
	UFUNCTION(CallInEditor, Category = Actions, meta = (DisplayPriority = 6))
	void Flip() { PostAction(ECubeGridToolAction::Flip); }
};

UCLASS()
class MESHMODELINGTOOLSEXP_API UCubeGridDuringActivityActions : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UCubeGridTool> ParentTool;

	void Initialize(UCubeGridTool* ParentToolIn) { ParentTool = ParentToolIn; }

	void PostAction(ECubeGridToolAction Action);

	/** Accept and complete current action. */
	UFUNCTION(CallInEditor, Category = Actions)
	void Done() { PostAction(ECubeGridToolAction::Done); }

	/** Cancel and exit current action */
	UFUNCTION(CallInEditor, Category = Actions)
	void Cancel() { PostAction(ECubeGridToolAction::Cancel); }
};

/** Tool that allows for blocky boolean operations on an orientable power-of-two grid. */
UCLASS()
class MESHMODELINGTOOLSEXP_API UCubeGridTool : public UInteractiveTool,
	public IClickDragBehaviorTarget, public IHoverBehaviorTarget,
	public UE::Geometry::IDynamicMeshOperatorFactory,
	public IInteractiveToolNestedAcceptCancelAPI
{
	GENERATED_BODY()
protected:
	enum class EMouseState
	{
		NotDragging,
		DraggingExtrudeDistance,
		DraggingCornerSelection,
		DraggingRegularSelection,
	};

	enum class EMode
	{
		PushPull,
		Corner,

		// This is currently not supported, but some of the code was written with space
		// for a "fit grid" mode that would have allowed the dimensions of the grid to
		// be fit using a sequence of (snapped) mouse clicks. It seems useful to leave 
		// those code stubs for now in case we add the mode in, so it's easier to track
		// down the affected portions of code.
		FitGrid
	};

	using FCubeGrid = UE::Geometry::FCubeGrid;

public:

	struct FSelection
	{
		// Both of these boxes are in the coordinate space of the (unscaled) grid frame.
		UE::Geometry::FAxisAlignedBox3d Box;
		UE::Geometry::FAxisAlignedBox3d StartBox; // Box delineating original selected face
		FCubeGrid::EFaceDirection Direction;

		bool operator==(const FSelection& Other)
		{
			return Box == Other.Box
				&& StartBox == Other.StartBox
				&& Direction == Other.Direction;
		}

		bool operator!=(const FSelection& Other)
		{
			return !(*this == Other);
		}
	};

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetTarget(TObjectPtr<UToolTarget> TargetIn) { Target = TargetIn; }
	virtual void SetWorld(UWorld* World) { TargetWorld = World; }

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

	virtual void RequestAction(ECubeGridToolAction ActionType);

	virtual void SetSelection(const FSelection& Selection, bool bEmitChange);
	virtual void ClearSelection(bool bEmitChange);

	// Used by undo/redo
	virtual void UpdateUsingMeshChange(const UE::Geometry::FDynamicMeshChange& MeshChange, bool bRevert);
	virtual bool IsInDefaultMode() const;
	virtual void RevertToDefaultMode();

	// IClickDragBehaviorTarget
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
	virtual void OnTerminateDragSequence() override;

	// IHoverBehaviorTarget
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override;
	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;

	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<UE::Geometry::FDynamicMeshOperator> MakeNewOperator() override;

	// IInteractiveToolNestedAcceptCancelAPI
	virtual bool SupportsNestedCancelCommand() override { return true; }
	virtual bool CanCurrentlyNestedCancel() override;
	virtual bool ExecuteNestedCancelCommand() override;
	virtual bool SupportsNestedAcceptCommand() override { return true; }
	virtual bool CanCurrentlyNestedAccept() override;
	virtual bool ExecuteNestedAcceptCommand() override;

protected:

	UPROPERTY()
	TObjectPtr<UCombinedTransformGizmo> GridGizmo = nullptr;

	UPROPERTY()
	TObjectPtr<UDragAlignmentMechanic> GridGizmoAlignmentMechanic = nullptr;
	
	UPROPERTY()
	TObjectPtr<UTransformProxy> GridGizmoTransformProxy = nullptr;

	UPROPERTY()
	TObjectPtr<UPreviewGeometry> LineSets = nullptr;

	UPROPERTY()
	TObjectPtr<UClickDragInputBehavior> ClickDragBehavior = nullptr;
	
	UPROPERTY()
	TObjectPtr<UMouseHoverBehavior> HoverBehavior = nullptr;

	UPROPERTY()
	TObjectPtr<ULocalSingleClickInputBehavior> CtrlMiddleClickBehavior = nullptr;

	UPROPERTY()
	TObjectPtr<ULocalClickDragInputBehavior> MiddleClickDragBehavior = nullptr;

	// Properties, etc
	UPROPERTY()
	TObjectPtr<UCubeGridToolProperties> Settings = nullptr;

	UPROPERTY()
	TObjectPtr<UCubeGridToolActions> ToolActions = nullptr;

	UPROPERTY()
	TObjectPtr<UCubeGridDuringActivityActions> DuringActivityActions = nullptr;

	UPROPERTY()
	TObjectPtr<UNewMeshMaterialProperties> MaterialProperties = nullptr;

	UPROPERTY()
	TObjectPtr<UCreateMeshObjectTypeProperties> OutputTypeProperties = nullptr;

	// Existing asset to modify, if one was selected
	UPROPERTY()
	TObjectPtr<UToolTarget> Target = nullptr;

	TSharedPtr<FCubeGrid, ESPMode::ThreadSafe> CubeGrid;

	// Where to make the preview, new mesh, etc
	TObjectPtr<UWorld> TargetWorld = nullptr;

	// Important state. Could refactor things into tool activities (like PolyEdit)
	// someday.
	EMode Mode = EMode::PushPull;
	EMouseState MouseState = EMouseState::NotDragging;

	bool GetHitGridFace(const FRay& WorldRay, UE::Geometry::FCubeGrid::FCubeFace& FaceOut);
	bool UpdateHover(const FRay& WorldRay);
	void UpdateHoverLineSet(bool bHaveHover, const UE::Geometry::FAxisAlignedBox3d& HoveredBox);
	void UpdateSelectionLineSet();
	void UpdateGridLineSet();
	void UpdateCornerModeLineSet();
	void ClearHover();

	bool bHaveSelection = false;
	FSelection Selection;

	bool bPreviousHaveSelection = false;
	FSelection PreviousSelection;

	bool bHaveHoveredSelection = false;
	UE::Geometry::FAxisAlignedBox3d HoveredSelectionBox;

	void SlideSelection(int32 ExtrudeAmount, bool bEmitChange);

	bool bSlideToggle = false;
	bool bSelectionToggle = false;
	bool bChangeSideToggle = false;
	bool bMouseDragShouldPushPull = false;
	FRay3d DragProjectionAxis;
	double DragProjectedStartParam;
	int32 DragStartExtrudeAmount = 0;

	void ApplyFlipSelection();
	void ApplySlide(int32 NumBlocks);
	void ApplyPushPull(int32 NumBlocks);

	// Parameter is signed on purpose so we can give negatives
	void SetPowerOfTwoClamped(int32 PowerOfTwo);
	uint8 PowerOfTwoPrevious;

	UPROPERTY()
	TObjectPtr<UMeshOpPreviewWithBackgroundCompute> Preview;

	/**
	 * @param bUpdateCornerLineSet This can be set to false when the invalidation
	 *  is a result of a grid transform change (which is applied to the line set
	 *  via the LineSets->SetTransform), or when the corner shape otherwise doesn't
	 *  change. Usually it can be left to true.
	 */
	void InvalidatePreview(bool bUpdateCornerLineSet = true);

	void ApplyPreview();

	int32 CurrentExtrudeAmount = 0;
	bool bPreviewMayDiffer = false;
	bool bWaitingToApplyPreview = false;
	bool bBlockUntilPreviewUpdate = false;
	bool bAdjustSelectionOnPreviewUpdate = false;

	TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> CurrentMesh;
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3, ESPMode::ThreadSafe> MeshSpatial;
	UE::Geometry::FTransformSRT3d CurrentMeshTransform = UE::Geometry::FTransformSRT3d::Identity();
	TSharedPtr<TArray<int32>, ESPMode::ThreadSafe> LastOpChangedTids;

	// Safe inputs for the background compute to use, untouched by undo/redo/other CurrentMesh updates.
	TSharedPtr<const UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> ComputeStartMesh;
	void UpdateComputeInputs();

	static const int32 ShiftModifierID = 1;
	static const int32 CtrlModifierID = 2;

	ECubeGridToolAction PendingAction = ECubeGridToolAction::NoAction;
	void ApplyAction(ECubeGridToolAction ActionType);

	int32 GridFrameOriginWatcherIdx;
	int32 GridFrameOrientationWatcherIdx;
	void GridGizmoMoved(UTransformProxy* Proxy, FTransform Transform);
	void UpdateGizmoVisibility(bool bVisible);
	bool bInGizmoDrag = false;

	FVector3d MiddleClickDragStart;
	FInputRayHit RayCastSelectionPlane(const FRay3d& WorldRay, 
		FVector3d& HitPointOut);
	FInputRayHit CanBeginMiddleClickDrag(const FInputDeviceRay& ClickPos);
	void OnMiddleClickDrag(const FInputDeviceRay& DragPos);
	void OnCtrlMiddleClick(const FInputDeviceRay& ClickPos);
	void PrepForSelectionChange();
	void EndSelectionChange();

	// Used in corner push/pull mode. If you create a flat FOrientedBox3d out of 
	// the current selection, with the Z axis being along selection normal, the 0-3
	// indices here correspond to the 0-3 corner indices in the box, which are the
	// bottom corners along Z axis (see TOrientedBox3::GetCorner())
	bool CornerSelectedFlags[4] = { false, false, false, false };
	bool PreDragCornerSelectedFlags[4] = { false, false, false, false };

	FViewCameraState CameraState;
	FToolDataVisualizer SelectedCornerRenderer;
	UE::Geometry::FGeometrySet3 CornersGeometrySet;
	void UpdateCornerGeometrySet();
	void StartCornerMode();
	void ApplyCornerMode(bool bDontWaitForTick = false);
	void CancelCornerMode();
	void AttemptToSelectCorner(const FRay3d& WorldRay);

	// Used to see if we need to update the asset that we've been modifying
	bool bChangesMade = false;
};
