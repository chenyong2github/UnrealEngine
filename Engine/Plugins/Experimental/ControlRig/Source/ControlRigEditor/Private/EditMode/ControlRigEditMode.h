// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "IPersonaEditMode.h"
#include "IControlRigObjectBinding.h"
#include "RigVMModel/RigVMGraph.h"
#include "Rigs/RigHierarchyContainer.h"
#include "Units/RigUnitContext.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "UObject/StrongObjectPtr.h"
#include "ControlRigEditMode.generated.h"

class FEditorViewportClient;
class FViewport;
class UActorFactory;
struct FViewportClick;
class UControlRig;
class FControlRigInteractionScope;
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
class UControlRigDetailPanelControlProxies;
class UControlRigControlsProxy;
struct FRigControl;
class IControlRigManipulatable;
class ISequencer;
enum class EControlRigSetKey : uint8;

DECLARE_DELEGATE_RetVal_ThreeParams(FTransform, FOnGetRigElementTransform, const FRigElementKey& /*RigElementKey*/, bool /*bLocal*/, bool /*bOnDebugInstance*/);
DECLARE_DELEGATE_ThreeParams(FOnSetRigElementTransform, const FRigElementKey& /*RigElementKey*/, const FTransform& /*Transform*/, bool /*bLocal*/);
DECLARE_DELEGATE_RetVal(TSharedPtr<FUICommandList>, FNewMenuCommandsDelegate);

class FControlRigEditMode;

UCLASS()
class UControlRigEditModeDelegateHelper : public UObject
{
	GENERATED_BODY()

public:

	UFUNCTION()
	void OnPoseInitialized();

	UFUNCTION()
	void PostPoseUpdate();

	void AddDelegates(USkeletalMeshComponent* InSkeletalMeshComponent);
	void RemoveDelegates();

	TWeakObjectPtr<USkeletalMeshComponent> BoundComponent;
	FControlRigEditMode* EditMode = nullptr;

private:
	FDelegateHandle OnBoneTransformsFinalizedHandle;
};

class FControlRigEditMode : public IPersonaEditMode
{
public:
	static FName ModeName;

	FControlRigEditMode();
	~FControlRigEditMode();

	/** Set the objects to be displayed in the details panel */
	void SetObjects(const TWeakObjectPtr<>& InSelectedObject,  UObject* BindingObject, TWeakPtr<ISequencer> InSequencer);

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
	virtual void PostUndo() override;

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
	FSimpleMulticastDelegate& OnAnimSystemInitialized() { return OnAnimSystemInitializedDelegate; }

	// callback that gets called when rig element is selected in other view
	void OnRigElementAdded(FRigHierarchyContainer* Container, const FRigElementKey& InKey);
	void OnRigElementRemoved(FRigHierarchyContainer* Container, const FRigElementKey& InKey);
	void OnRigElementRenamed(FRigHierarchyContainer* Container, ERigElementType ElementType, const FName& InOldName, const FName& InNewName);
	void OnRigElementReparented(FRigHierarchyContainer* Container, const FRigElementKey& InKey, const FName& InOldParentName, const FName& InNewParentName);
	void OnRigElementSelected(FRigHierarchyContainer* Container, const FRigElementKey& InKey, bool bSelected);
	void OnRigElementChanged(FRigHierarchyContainer* Container, const FRigElementKey& InKey);
	void OnControlUISettingChanged(FRigHierarchyContainer* Container, const FRigElementKey& InKey);
	void OnControlModified(UControlRig* Subject, const FRigControl& Control, const FRigControlModifiedContext& Context);

	/** return true if it can be removed from preview scene 
	- this is to ensure preview scene doesn't remove Gizmo actors */
	bool CanRemoveFromPreviewScene(const USceneComponent* InComponent);

	FUICommandList* GetCommandBindings() const { return CommandBindings.Get(); }

protected:

	// Gizmo related functions wrt enable/selection
	/** Get the node name from the property path */
	AControlRigGizmoActor* GetGizmoFromControlName(const FName& InControlName) const;

protected:
	/** Helper function: set ControlRigs array to the details panel */
	void SetObjects_Internal();

	/** Set up Details Panel based upon Selected Objects*/
	void SetUpDetailPanel();

	/** Updates cached pivot transform */
	void RecalcPivotTransform();

	/** Helper function for box/frustum intersection */
	bool IntersectSelect(bool InSelect, const TFunctionRef<bool(const AControlRigGizmoActor*, const FTransform&)>& Intersects);

	/** Handle selection internally */
	void HandleSelectionChanged();

	/** Toggles visibility of manipulators in the viewport */
	void ToggleManipulators();

	/** Clear Selection*/
	void ClearSelection();

	/** Frame to current Control Selection*/
	void FrameSelection();

	/** Whether or not we should Frame Selection or not*/
	bool CanFrameSelection();

	/** Reset Transforms */
	void ResetTransforms(bool bSelectionOnly);

	/** Increase Gizmo Size */
	void IncreaseGizmoSize();

	/** Decrease Gizmo Size */
	void DecreaseGizmoSize();

	/** Reset Gizmo Size */
	void ResetGizmoSize();

	/** Bind our keyboard commands */
	void BindCommands();

	/** It creates if it doesn't have it */
	void RecreateGizmoActors(const TArray<FRigElementKey>& InSelectedElements = TArray<FRigElementKey>());

	/** Requests to recreate the gizmo actors in the next tick */
	void RequestToRecreateGizmoActors() { bRecreateGizmosRequired = true; }

	/** Let the preview scene know how we want to select components */
	bool GizmoSelectionOverride(const UPrimitiveComponent* InComponent) const;

protected:

	TWeakPtr<ISequencer> WeakSequencer;

	/** Settings object used to insert controls into the details panel */
	UControlRigEditModeSettings* Settings;

	/** The scope for the interaction */
	FControlRigInteractionScope* InteractionScope;

	/** Whether a manipulator actually made a change when transacting */
	bool bManipulatorMadeChange;

	/** Guard value for selection */
	bool bSelecting;

	/** If selection was changed, we set up proxies on next tick */
	bool bSelectionChanged;

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
	FSimpleMulticastDelegate OnAnimSystemInitializedDelegate;
	
	TArray<FRigElementKey> SelectedRigElements;

	/* Flag to recreate gizmos during tick */
	bool bRecreateGizmosRequired;

	/** Gizmo actors */
	TArray<AControlRigGizmoActor*> GizmoActors;
	UControlRigDetailPanelControlProxies* ControlProxy;

	/** Utility functions for UI/Some other viewport manipulation*/
	bool IsControlSelected() const;
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

	void SetSelectedRigElement(const FName& InElementName, ERigElementType InRigElementType);

	/** Set multiple RigElement's selection states */
	void SetRigElementSelection(ERigElementType Type, const TArray<FName>& InRigElementNames, bool bSelected);

	/** Check if any RigElements are selected */
	bool AreRigElementsSelected(uint32 InTypes) const;

	/** Get the number of selected RigElements */
	int32 GetNumSelectedRigElements(uint32 InTypes) const;

	UControlRig* GetControlRig(bool bInteractionRig, int32 InIndex = 0) const;

private:
	/** Set a RigElement's selection state */
	void SetRigElementSelectionInternal(ERigElementType Type, const FName& InRigElementName, bool bSelected);
	
	FEditorViewportClient* CurrentViewportClient;

/* store coordinate system per widget mode*/
private:
	void OnWidgetModeChanged(FWidget::EWidgetMode InWidgetMode);
	void OnCoordSystemChanged(ECoordSystem InCoordSystem);
	TArray<ECoordSystem> CoordSystemPerWidgetMode;
	bool bIsChangingCoordSystem;

private:

	bool CreateGizmoActors(UWorld* World);
	void DestroyGizmosActors();

	void AddControlRig(UControlRig* InControlRig);
	void RemoveControlRig(UControlRig* InControlRig);
	void TickManipulatableObjects(float DeltaTime);

	void SetGizmoTransform(AControlRigGizmoActor* GizmoActor, const FTransform& InTransform);
	FTransform GetGizmoTransform(AControlRigGizmoActor* GizmoActor) const;
	void MoveGizmo(AControlRigGizmoActor* GizmoActor, const bool bTranslation, FVector& InDrag, 
		const bool bRotation, FRotator& InRot, const bool bScale, FVector& InScale, const FTransform& ToWorldTransform,
		bool bUseLocal, bool bCalcLocal, FTransform& InOutLocal);
	void TickGizmo(AControlRigGizmoActor* GizmoActor, const FTransform& ComponentTransform);
	bool ModeSupportedByGizmoActor(const AControlRigGizmoActor* GizmoActor, FWidget::EWidgetMode InMode) const;

	// Object binding
	/** Setup bindings to a runtime object (or clear by passing in nullptr). */
	void SetObjectBinding(TSharedPtr<IControlRigObjectBinding> InObjectBinding);
	/** Get bindings to a runtime object */
	TSharedPtr<IControlRigObjectBinding> GetObjectBinding() const;

	USceneComponent* GetHostingSceneComponent() const;
	FTransform	GetHostingSceneComponentTransform() const;

private:

	// Post pose update handler
	void OnPoseInitialized();
	void PostPoseUpdate();

	// world clean up handlers
	FDelegateHandle OnWorldCleanupHandle;
	void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	UWorld* WorldPtr = nullptr;

	TArray<TWeakObjectPtr<UControlRig>> RuntimeControlRigs;

	TStrongObjectPtr<UControlRigEditModeDelegateHelper> DelegateHelper;

	friend class FControlRigEditorModule;
	friend class FControlRigEditor;
	friend class FControlRigEditModeGenericDetails;
	friend class UControlRigEditModeDelegateHelper;
	friend class SControlRigEditModeTools;
};
