// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "InteractiveTool.h"
#include "InteractiveToolBuilder.h"
#include "MultiSelectionTool.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "Framework/Commands/UICommandInfo.h"
#include "BaseGizmos/TransformProxy.h"
#include "BaseSequencerAnimTool.h"
#include "EditPivotTool.generated.h"

class USingleClickInputBehavior;
class UClickDragInputBehavior;
class UTransformGizmo;
class ULevelSequence;
class UControlRig;
struct FRigControlElement;
class ISequencer;

/*
*  The way this sequencer pivot tool works is that
*  it will use modify the incoming selections temp pivot
*  while the mode is active. Reselecting will turn off the mode.
*
*/

/**
 * Builder for USequencerPivotTool
 */
UCLASS()
class  USequencerPivotToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};

struct FSelectionDuringDrag
{
	ULevelSequence* LevelSequence;
	FFrameNumber CurrentFrame;
	FTransform CurrentTransform;

};
struct FControlRigSelectionDuringDrag : public FSelectionDuringDrag
{
	UControlRig* ControlRig;
	FName ControlName;
};

struct FActorSelectonDuringDrag : public FSelectionDuringDrag
{
	AActor* Actor;
};

struct FControlRigMappings
{
	TWeakObjectPtr<UControlRig> ControlRig;
	TMap<FName, FTransform> PivotTransforms;
};

struct FActorMappings
{
	TWeakObjectPtr<AActor> Actor;
	FTransform PivotTransform;
};

struct FSavedMappings
{
	TMap<TWeakObjectPtr<UControlRig>, FControlRigMappings> ControlRigMappings;
	TMap<TWeakObjectPtr<AActor>, FActorMappings> ActorMappings;
};

struct FLastSelectedObjects
{
	TArray<FControlRigMappings> LastSelectedControlRigs;
	TArray<FActorMappings> LastSelectedActors;

};
class FEditPivotCommands : public TCommands<FEditPivotCommands>
{
public:
	FEditPivotCommands()
		: TCommands<FEditPivotCommands>(
			"SequencerEditPivotTool",
			NSLOCTEXT("SequencerEditPivotTool", "SequencerEditPivotTool", "Edit Pivot Commands"),
			NAME_None,
			FEditorStyle::GetStyleSetName()
			)
	{}


	virtual void RegisterCommands() override;
	static const TMap<FName, TArray<TSharedPtr<FUICommandInfo>>>& GetCommands()
	{
		return FEditPivotCommands::Get().Commands;
	}

public:
	/** Reset the Pivot*/
	TSharedPtr<FUICommandInfo> ResetPivot;

	TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> Commands;
};


/**
Pivot tool class
 */
UCLASS()
class  USequencerPivotTool : public UMultiSelectionTool, public IClickBehaviorTarget  , public IBaseSequencerAnimTool
{
	GENERATED_BODY()

public:

	// UInteractiveTool overrides
	virtual void SetWorld(UWorld* World, UInteractiveGizmoManager* GizmoManager);
	virtual void Setup() override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual bool HasCancel() const override { return false; }
	virtual bool HasAccept() const override { return false; }
	virtual bool CanAccept() const override { return false; }

	// IClickBehaviorTarget interface
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

	//IBaseSequencerAnimTool
	virtual bool ProcessCommandBindings(const FKey Key, const bool bRepeat) const override;

	// End interfaces

protected:

	UPROPERTY()
	USingleClickInputBehavior* ClickBehavior = nullptr;

	UPROPERTY()
	UTransformProxy* TransformProxy = nullptr;

	UPROPERTY()
	UTransformGizmo* TransformGizmo = nullptr;

protected:
	bool bShiftPressedWhenStarted = false;
	bool bCtrlPressedWhenStarted = false;
	int32 CtrlModifierId = 1;
	UWorld* TargetWorld = nullptr;		// target World we will raycast into
	UInteractiveGizmoManager* GizmoManager = nullptr; //gizmo man

	FTransform StartDragTransform;
	bool bGizmoBeingDragged = false;
	bool bManipulatorMadeChange = false;
	int32 TransactionIndex = -1;
	TArray<FControlRigSelectionDuringDrag> ControlRigDrags;
	TArray<FActorSelectonDuringDrag> ActorDrags;

	//since we are selection based we can cache this
	ULevelSequence* LevelSequence;
	TArray<TWeakObjectPtr<UControlRig>> ControlRigs;
	TWeakPtr<ISequencer> SequencerPtr;
	TArray<TWeakObjectPtr<AActor>> Actors;

	FTransform GizmoTransform = FTransform::Identity;
	bool bPickingPivotLocation = false; //mauy remove
	void UpdateGizmoVisibility();
	void UpdateGizmoTransform();
	void UpdateTransformAndSelectionOnEntering();
	bool SetGizmoBasedOnSelection(bool bUseSaved = true);

	FInputRayHit FindRayHit(const FRay& WorldRay, FVector& HitPos);		// raycasts into World

	// Callbacks we'll receive from the gizmo proxy
	void GizmoTransformChanged(UTransformProxy* Proxy, FTransform Transform);
	void GizmoTransformStarted(UTransformProxy* Proxy);
	void GizmoTransformEnded(UTransformProxy* Proxy);

	//Handle Selection and Pivot Location
	void SavePivotTransforms();
	void SaveLastSelected();

	// selection delegates
	void DeactivateMe();
	void RemoveDelegates();
	void HandleControlSelected(UControlRig* Subject, FRigControlElement* InControl, bool bSelected);
	void OnEditorSelectionChanged(UObject* NewSelection);
	FDelegateHandle OnEditorSelectionChangedHandle;

private:
	TSharedPtr<FUICommandList> CommandBindings;
	void ResetPivot();

public:
	static FSavedMappings SavedPivotLocations;
	static FLastSelectedObjects LastSelectedObjects;
};


