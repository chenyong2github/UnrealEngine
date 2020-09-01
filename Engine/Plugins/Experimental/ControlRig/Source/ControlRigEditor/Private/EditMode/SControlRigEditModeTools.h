// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "IDetailKeyframeHandler.h"
#include "RigVMModel/RigVMGraph.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FControlRigEditMode;
class IDetailsView;
class ISequencer;
class SControlPicker;
class SExpandableArea;
class SControlHierarchy;
class UControlRig;

class SControlRigEditModeTools : public SCompoundWidget, public IDetailKeyframeHandler
{
public:
	SLATE_BEGIN_ARGS(SControlRigEditModeTools) {}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, FControlRigEditMode& InEditMode, UWorld* InWorld);

	/** Set the objects to be displayed in the details panel */
	void SetDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects);

	/** Set the sequencer we are bound to */
	void SetSequencer(TWeakPtr<ISequencer> InSequencer);

	/** Set The Control Rig we are using*/
	void SetControlRig(UControlRig* ControlRig);

	// IDetailKeyframeHandler interface
	virtual bool IsPropertyKeyable(UClass* InObjectClass, const class IPropertyHandle& PropertyHandle) const override;
	virtual bool IsPropertyKeyingEnabled() const override;
	virtual void OnKeyPropertyClicked(const IPropertyHandle& KeyedPropertyHandle) override;
	virtual bool IsPropertyAnimated(const class IPropertyHandle& PropertyHandle, UObject *ParentObject) const override;
private:
	/** Sequencer we are currently bound to */
	TWeakPtr<ISequencer> WeakSequencer;

	/** The details view we do most of our work within */
	TSharedPtr<IDetailsView> ControlDetailsView;

	/** Expander to interact with the options of the rig  */
	TSharedPtr<SExpandableArea> RigOptionExpander;
	TSharedPtr<IDetailsView> RigOptionsDetailsView;

	/** Hierarchy picker for controls*/
	TSharedPtr<SControlHierarchy> ControlHierarchy;

	/** Special picker for controls, no longer used */
	TSharedPtr<SControlPicker> ControlPicker;
	TSharedPtr<SExpandableArea> PickerExpander;

	/** Storage for both sequencer and viewport rigs */
	TWeakObjectPtr<UControlRig> SequencerRig;
	TWeakObjectPtr<UControlRig> ViewportRig;

	/** Display or edit set up for property */
	bool ShouldShowPropertyOnDetailCustomization(const struct FPropertyAndParent& InPropertyAndParent) const;
	bool IsReadOnlyPropertyOnDetailCustomization(const struct FPropertyAndParent& InPropertyAndParent) const;

	/** Called when a manipulator is selected in the picker */
	void OnManipulatorsPicked(const TArray<FName>& Manipulators);

	void HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);

	EVisibility GetRigOptionExpanderVisibility() const;

	void OnRigOptionFinishedChange(const FPropertyChangedEvent& PropertyChangedEvent);
};
