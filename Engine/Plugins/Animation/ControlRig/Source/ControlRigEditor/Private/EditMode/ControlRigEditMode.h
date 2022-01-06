// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "IControlRigObjectBinding.h"
#include "RigVMModel/RigVMGraph.h"
#include "Rigs/RigHierarchyContainer.h"
#include "Units/RigUnitContext.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealWidgetFwd.h"
#include "IControlRigEditMode.h"
#include "ControlRigEditMode.generated.h"



class FEditorViewportClient;
class FViewport;
class UActorFactory;
struct FViewportClick;
class UControlRig;
class FControlRigInteractionScope;
class ISequencer;
class UControlManipulator;
class FUICommandList;
class FPrimitiveDrawInterface;
class FToolBarBuilder;
class FExtender;
class IMovieScenePlayer;
class AControlRigShapeActor;
class UDefaultControlRigManipulationLayer;
class UControlRigDetailPanelControlProxies;
class UControlRigControlsProxy;
struct FRigControl;
class IControlRigManipulatable;
class ISequencer;
enum class EControlRigSetKey : uint8;
class UToolMenu;

DECLARE_DELEGATE_RetVal_ThreeParams(FTransform, FOnGetRigElementTransform, const FRigElementKey& /*RigElementKey*/, bool /*bLocal*/, bool /*bOnDebugInstance*/);
DECLARE_DELEGATE_ThreeParams(FOnSetRigElementTransform, const FRigElementKey& /*RigElementKey*/, const FTransform& /*Transform*/, bool /*bLocal*/);
DECLARE_DELEGATE_RetVal(TSharedPtr<FUICommandList>, FNewMenuCommandsDelegate);
DECLARE_MULTICAST_DELEGATE_TwoParams(FControlRigAddedOrRemoved, UControlRig*, bool /*true if added, false if removed*/);
DECLARE_DELEGATE_RetVal(UToolMenu*, FOnGetContextMenu);

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

class FControlRigEditMode : public IControlRigEditMode
{
public:
	static FName ModeName;

	FControlRigEditMode();
	~FControlRigEditMode();

	/** Set the objects to be displayed in the details panel */
	virtual void SetObjects(const TWeakObjectPtr<>& InSelectedObject,  UObject* BindingObject, TWeakPtr<ISequencer> InSequencer) override;

	/** This edit mode is re-used between the level editor and the control rig editor. Calling this indicates which context we are in */
	bool IsInLevelEditor() const;

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
	virtual bool UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const;
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

	/** Find the edit mode corresponding to the specified world context */
	static FControlRigEditMode* GetEditModeFromWorldContext(UWorld* InWorldContext);

	/** Bone Manipulation Delegates */
	FOnGetRigElementTransform& OnGetRigElementTransform() { return OnGetRigElementTransformDelegate; }
	FOnSetRigElementTransform& OnSetRigElementTransform() { return OnSetRigElementTransformDelegate; }

	/** Context Menu Delegates */
	FOnGetContextMenu& OnGetContextMenu() { return OnGetContextMenuDelegate; }
	FNewMenuCommandsDelegate& OnContextMenuCommands() { return OnContextMenuCommandsDelegate; }
	FSimpleMulticastDelegate& OnAnimSystemInitialized() { return OnAnimSystemInitializedDelegate; }

	/* Control Rig Changed Delegate*/
	FControlRigAddedOrRemoved& OnControlRigAddedOrRemoved() { return OnControlRigAddedOrRemovedDelegate; }

	// callback that gets called when rig element is selected in other view
	void OnHierarchyModified(ERigHierarchyNotification InNotif, URigHierarchy* InHierarchy, const FRigBaseElement* InElement);
	void OnControlModified(UControlRig* Subject, FRigControlElement* InControlElement, const FRigControlModifiedContext& Context);

	/** return true if it can be removed from preview scene 
	- this is to ensure preview scene doesn't remove shape actors */
	bool CanRemoveFromPreviewScene(const USceneComponent* InComponent);

	FUICommandList* GetCommandBindings() const { return CommandBindings.Get(); }

	/** Requests to recreate the shape actors in the next tick */
	void RequestToRecreateControlShapeActors() { bRecreateControlShapesRequired = true; }

protected:

	// shape related functions wrt enable/selection
	/** Get the node name from the property path */
	AControlRigShapeActor* GetControlShapeFromControlName(const FName& InControlName) const;

protected:
	/** Helper function: set ControlRigs array to the details panel */
	void SetObjects_Internal();

	/** Set up Details Panel based upon Selected Objects*/
	void SetUpDetailPanel();

	/** Updates cached pivot transform */
	void RecalcPivotTransform();

	/** Helper function for box/frustum intersection */
	bool IntersectSelect(bool InSelect, const TFunctionRef<bool(const AControlRigShapeActor*, const FTransform&)>& Intersects);

	/** Handle selection internally */
	void HandleSelectionChanged();

	/** Toggles visibility of manipulators in the viewport */
	void ToggleManipulators();

public:
	
	/** Clear Selection*/
	void ClearSelection();

	/** Frame to current Control Selection*/
	void FrameSelection();

	/** Frame a list of provided items*/
   	void FrameItems(const TArray<FRigElementKey>& InItems);

	/** Opens up the space picker widget */
	void OpenSpacePickerWidget();

private:
	
	/** Whether or not we should Frame Selection or not*/
	bool CanFrameSelection();

	/** Reset Transforms */
	void ResetTransforms(bool bSelectionOnly);

	/** Increase Shape Size */
	void IncreaseShapeSize();

	/** Decrease Shape Size */
	void DecreaseShapeSize();

	/** Reset Shape Size */
	void ResetControlShapeSize();

public:
	
	/** Toggle Shape Transform Edit*/
	void ToggleControlShapeTransformEdit();

private:
	
	/** The hotkey text is passed to a viewport notification to inform users how to toggle shape edit*/
	FText GetToggleControlShapeTransformEditHotKey() const;

	/** Bind our keyboard commands */
	void BindCommands();

	/** It creates if it doesn't have it */
	void RecreateControlShapeActors(const TArray<FRigElementKey>& InSelectedElements = TArray<FRigElementKey>());


	/** Let the preview scene know how we want to select components */
	bool ShapeSelectionOverride(const UPrimitiveComponent* InComponent) const;

	/** Enable editing of control's shape transform instead of control's transform*/
	bool bIsChangingControlShapeTransform;

protected:

	TWeakPtr<ISequencer> WeakSequencer;

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
	FOnGetContextMenu OnGetContextMenuDelegate;
	FNewMenuCommandsDelegate OnContextMenuCommandsDelegate;
	FSimpleMulticastDelegate OnAnimSystemInitializedDelegate;
	FControlRigAddedOrRemoved OnControlRigAddedOrRemovedDelegate;

	/** GetSelectedRigElements */
	TArray<FRigElementKey> GetSelectedRigElements() const;

	/* Flag to recreate shapes during tick */
	bool bRecreateControlShapesRequired;

	/* Flag to temporarily disable handling notifs from the hierarchy */
	bool bSuspendHierarchyNotifs;

	/** Shape actors */
	TArray<AControlRigShapeActor*> ShapeActors;
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

	/** Get the control rig*/
	UControlRig* GetControlRig(bool bInteractionRig, int32 InIndex = 0) const;

	/** Get the detail proxiescontrol rig*/
	UControlRigDetailPanelControlProxies* GetDetailProxies() { return ControlProxy; }

	/** Get Sequencer Driving This*/
	TWeakPtr<ISequencer> GetWeakSequencer() { return WeakSequencer; }

	/** Suspend Rig Hierarchy Notifies*/
	void SuspendHierarchyNotifs(bool bVal) { bSuspendHierarchyNotifs = bVal; }
private:
	/** Set a RigElement's selection state */
	void SetRigElementSelectionInternal(ERigElementType Type, const FName& InRigElementName, bool bSelected);
	
	FEditorViewportClient* CurrentViewportClient;

/* store coordinate system per widget mode*/
private:
	void OnWidgetModeChanged(UE::Widget::EWidgetMode InWidgetMode);
	void OnCoordSystemChanged(ECoordSystem InCoordSystem);
	TArray<ECoordSystem> CoordSystemPerWidgetMode;
	bool bIsChangingCoordSystem;

	bool CanChangeControlShapeTransform();
public:
	//Toolbar functions
	void SetOnlySelectRigControls(bool val);
	bool GetOnlySelectRigControls()const;

private:
	TSet<FName> GetActiveControlsFromSequencer(UControlRig* ControlRig);
	bool CreateShapeActors(UWorld* World);
	void DestroyShapesActors();

	void AddControlRig(UControlRig* InControlRig);
	void RemoveControlRig(UControlRig* InControlRig);
	void TickManipulatableObjects(float DeltaTime);

	void SetControlShapeTransform(AControlRigShapeActor* ShapeActor, const FTransform& InTransform);
	FTransform GetControlShapeTransform(AControlRigShapeActor* ShapeActor) const;
	void MoveControlShape(AControlRigShapeActor* ShapeActor, const bool bTranslation, FVector& InDrag, 
		const bool bRotation, FRotator& InRot, const bool bScale, FVector& InScale, const FTransform& ToWorldTransform,
		bool bUseLocal, bool bCalcLocal, FTransform& InOutLocal);

	void ChangeControlShapeTransform(AControlRigShapeActor* ShapeActor, const bool bTranslation, FVector& InDrag,
		const bool bRotation, FRotator& InRot, const bool bScale, FVector& InScale, const FTransform& ToWorldTransform);

	void TickControlShape(AControlRigShapeActor* ShapeActor, const FTransform& ComponentTransform);
	bool ModeSupportedByShapeActor(const AControlRigShapeActor* ShapeActor, UE::Widget::EWidgetMode InMode) const;

	// Object binding
	/** Setup bindings to a runtime object (or clear by passing in nullptr). */
	void SetObjectBinding(TSharedPtr<IControlRigObjectBinding> InObjectBinding);

protected:
	
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

	TArray<FRigElementKey> DeferredItemsToFrame;

	friend class FControlRigEditorModule;
	friend class FControlRigEditor;
	friend class FControlRigEditModeGenericDetails;
	friend class UControlRigEditModeDelegateHelper;
	friend class SControlRigEditModeTools;
};
