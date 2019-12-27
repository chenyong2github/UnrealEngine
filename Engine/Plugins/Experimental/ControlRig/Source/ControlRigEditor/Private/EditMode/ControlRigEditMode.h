// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "IPersonaEditMode.h"
#include "ControlRigModel.h"
#include "Rigs/RigHierarchyContainer.h"
#include "Drawing/ControlRigDrawInterface.h"
#include "Units/RigUnitContext.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

class FEditorViewportClient;
class FViewport;
class UActorFactory;
struct FViewportClick;
class UControlRig;
class ISequencer;
class UControlRigEditModeSettings;
class UControlManipulator;
class FUICommandList;
class FPrimitiveDrawInterface;
class FToolBarBuilder;
class FExtender;
class IMovieScenePlayer;
class AControlRigGizmoActor;
class UDefaultControlRigManipulationLayer;

DECLARE_DELEGATE_RetVal_TwoParams(FTransform, FOnGetRigElementTransform, const FRigElementKey& /*RigElementKey*/, bool /*bLocal*/);
DECLARE_DELEGATE_ThreeParams(FOnSetRigElementTransform, const FRigElementKey& /*RigElementKey*/, const FTransform& /*Transform*/, bool /*bLocal*/);
DECLARE_DELEGATE_RetVal(TSharedPtr<FUICommandList>, FNewMenuCommandsDelegate);

class FControlRigEditMode : public IPersonaEditMode
{
public:
	static FName ModeName;

	FControlRigEditMode();
	~FControlRigEditMode();

	/** Set the objects to be displayed in the details panel */
	void SetObjects(const TWeakObjectPtr<>& InSelectedObject, const FGuid& InObjectBinding, UObject* BindingObject);

	/** This edit mode is re-used between the level editor and the control rig editor. Calling this indicates which context we are in */
	virtual bool IsInLevelEditor() const { return true; }

	// FEdMode interface
	virtual bool UsesToolkits() const override;
	virtual void Enter() override;
	virtual void Exit() override;
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual bool InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent) override;
	virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy *HitProxy, const FViewportClick &Click) override;
	virtual bool BoxSelect(FBox& InBox, bool InSelect = true) override;
	virtual bool FrustumSelect(const FConvexVolume& InFrustum, FEditorViewportClient* InViewportClient, bool InSelect = true) override;
	virtual void SelectNone() override;
	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override;
	virtual bool UsesTransformWidget() const override;
	virtual bool UsesTransformWidget(FWidget::EWidgetMode CheckMode) const;
	virtual FVector GetWidgetLocation() const override;
	virtual bool GetCustomDrawingCoordinateSystem(FMatrix& OutMatrix, void* InData) override;
	virtual bool GetCustomInputCoordinateSystem(FMatrix& OutMatrix, void* InData) override;
	virtual bool ShouldDrawWidget() const override;
	virtual bool IsCompatibleWith(FEditorModeID OtherModeID) const override;
	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override;
	virtual bool MouseLeave(FEditorViewportClient* ViewportClient, FViewport* Viewport);

	/* IPersonaEditMode interface */
	virtual bool GetCameraTarget(FSphere& OutTarget) const override { return false; }
	virtual class IPersonaPreviewScene& GetAnimPreviewScene() const override { check(false); return *(IPersonaPreviewScene*)this; }
	virtual void GetOnScreenDebugInfo(TArray<FText>& OutDebugInfo) const override {}

	/** FGCObject interface */
	virtual void AddReferencedObjects( FReferenceCollector& Collector ) override;

	/** Refresh our internal object list (they may have changed) */
	void RefreshObjects();

	/** Get the settings we are using */
	const UControlRigEditModeSettings* GetSettings() { return Settings; }

	/** Find the edit mode corresponding to the specified world context */
	static FControlRigEditMode* GetEditModeFromWorldContext(UWorld* InWorldContext);

	/** Bone Manipulation Delegates */
	FOnGetRigElementTransform& OnGetRigElementTransform() { return OnGetRigElementTransformDelegate; }
	FOnSetRigElementTransform& OnSetRigElementTransform() { return OnSetRigElementTransformDelegate; }

	/** Context Menu Delegates */
	FNewMenuDelegate& OnContextMenu() { return OnContextMenuDelegate; }
	FNewMenuCommandsDelegate& OnContextMenuCommands() { return OnContextMenuCommandsDelegate; }

	// callback that gets called when rig element is selected in other view
	void OnRigElementAdded(FRigHierarchyContainer* Container, const FRigElementKey& InKey);
	void OnRigElementRemoved(FRigHierarchyContainer* Container, const FRigElementKey& InKey);
	void OnRigElementRenamed(FRigHierarchyContainer* Container, ERigElementType ElementType, const FName& InOldName, const FName& InNewName);
	void OnRigElementReparented(FRigHierarchyContainer* Container, const FRigElementKey& InKey, const FName& InOldParentName, const FName& InNewParentName);
	void OnRigElementSelected(FRigHierarchyContainer* Container, const FRigElementKey& InKey, bool bSelected);
	void OnRigElementChanged(FRigHierarchyContainer* Container, const FRigElementKey& InKey);
	void OnControlUISettingChanged(FRigHierarchyContainer* Container, const FRigElementKey& InKey);

	/** Enable RigElement Editing */
	void EnableRigElementEditing(bool bEnabled);

	/** Get Control Rig, could be more than one later,  we are animating. Currently used by Sequencer for cross selection.*/
	UControlRig*  GetControlRig() { return WeakControlRigEditing.IsValid() ? WeakControlRigEditing.Get() : nullptr; }
protected:

	// Gizmo related functions wrt enable/selection
	/** Get the node name from the property path */
	AControlRigGizmoActor* GetGizmoFromControlName(const FName& InControlName) const;

protected:
	/** Helper function: set ControlRigs array to the details panel */
	void SetObjects_Internal();

	/** Updates cached pivot transform */
	void RecalcPivotTransform();

	/** Helper function for box/frustum intersection */
	bool IntersectSelect(bool InSelect, const TFunctionRef<bool(const AControlRigGizmoActor*, const FTransform&)>& Intersects);

	/** Handle selection internally */
	void HandleSelectionChanged();

	/** Toggles visibility of manipulators in the viewport */
	void ToggleManipulators();

	/** Bind our keyboard commands */
	void BindCommands();

	/** It creates if it doesn't have it */
	void RecreateManipulationLayer();

	/** Requests to recreate the manipulation layer in the next tick */
	void RequestToRecreateManipulationLayer() { bRecreateManipulationLayerRequired = true; }

	/** Let the preview scene know how we want to select components */
	bool GizmoSelectionOverride(const UPrimitiveComponent* InComponent) const;

protected:
	/** Settings object used to insert controls into the details panel */
	UControlRigEditModeSettings* Settings;

	/** Whether we are in the middle of a transaction */
	bool bIsTransacting;

	/** Whether a manipulator actually made a change when transacting */
	bool bManipulatorMadeChange;

	/** The ControlRig we are animating as a main. This is the main editing object we're working on */
	TWeakObjectPtr<UControlRig> WeakControlRigEditing;
	/** The sequencer GUID of the object we are animating */
	FGuid ControlRigGuid;

	/** The draw interface to use for the control rig */
	FControlRigDrawInterface DrawInterface;

	/** Guard value for selection */
	bool bSelecting;

	/** Cached transform of pivot point for selected Bones */
	FTransform PivotTransform;

	/** Command bindings for keyboard shortcuts */
	TSharedPtr<FUICommandList> CommandBindings;

	/** Called from the editor when a blueprint object replacement has occurred */
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& OldToNewInstanceMap);

	/** Return true if transform setter/getter delegates are available */
	bool IsTransformDelegateAvailable() const;

	FOnGetRigElementTransform OnGetRigElementTransformDelegate;
	FOnSetRigElementTransform OnSetRigElementTransformDelegate;
	FNewMenuDelegate OnContextMenuDelegate;
	FNewMenuCommandsDelegate OnContextMenuCommandsDelegate;
	
	TArray<FRigElementKey> SelectedRigElements;

	/* Flag to enable element init pose editing @Todo: */
	bool bEnableRigElementDefaultPoseEditing;

	/* Flag to recreate manipulation layer during tick */
	bool bRecreateManipulationLayerRequired;

	/** Default Manipulation Layer */
	UDefaultControlRigManipulationLayer* ManipulationLayer;
	TArray<AControlRigGizmoActor*> GizmoActors;

	/** Utility functions for UI/Some other viewport manipulation*/
	bool IsControlSelected() const;
	bool IsControlOrSpaceOrBoneSelected() const;
	bool AreRigElementSelectedAndMovable() const;
	
	/** Set initial transform handlers */
	void OpenContextMenu(FEditorViewportClient* InViewportClient);

	/* Set initial transform helpers*/
	bool GetRigElementGlobalTransform(const FRigElementKey& InElement, FTransform& OutGlobalTransform) const;
	
public: 
	/** Clear all selected RigElements */
	void ClearRigElementSelection(uint32 InTypes);

	/** Set a RigElement's selection state */
	void SetRigElementSelection(ERigElementType Type, const FName& InRigElementName, bool bSelected);

	/** Set multiple RigElement's selection states */
	void SetRigElementSelection(ERigElementType Type, const TArray<FName>& InRigElementNames, bool bSelected);

	/** Check if any RigElements are selected */
	bool AreRigElementsSelected(uint32 InTypes) const;

	/** Get the number of selected RigElements */
	int32 GetNumSelectedRigElements(uint32 InTypes) const;

private:
	/** Set a RigElement's selection state */
	void SetRigElementSelectionInternal(ERigElementType Type, const FName& InRigElementName, bool bSelected);

	FEditorViewportClient* CurrentViewportClient;

	friend class FControlRigEditorModule;
	friend class UControlRigPickerWidget;
	friend class FControlRigEditor;
};
