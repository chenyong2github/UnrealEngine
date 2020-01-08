// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
#include "Rigs/RigControlHierarchy.h"
#include "Rigs/RigSpaceHierarchy.h"
#include "ControlRigGizmoLibrary.h"

/**
 *
 * IControlRigManipulatable provides an interface for subjects who desire to be 
 * manipulatable by the Control Rig manipulation framework.
 * The manipulatable provides opaque access to available controls / spaces
 * and implements a series of setter functions to perform changes.
 *
 */

class IControlRigObjectBinding;

class CONTROLRIG_API IControlRigManipulatable
{
public:
	IControlRigManipulatable();
	virtual ~IControlRigManipulatable();

	/** Bindable event for external objects to contribute to / filter a control value */
	DECLARE_EVENT_ThreeParams(IControlRigManipulatable, FFilterControlEvent, IControlRigManipulatable*, const FRigControl&, FRigControlValue&);

	/** Bindable event for external objects to be notified of Control changes */
	DECLARE_EVENT_TwoParams(IControlRigManipulatable, FControlModifiedEvent, IControlRigManipulatable*, const FRigControl&);
#if WITH_EDITOR
	/** Bindable event for external objects to be notified that a Control is Selected */
	DECLARE_EVENT_ThreeParams(IControlRigManipulatable, FControlSelectedEvent, IControlRigManipulatable*, const FRigControl&,  bool);
#endif

	// Returns true if this manipulatable subject is currently
	// available for manipulation / is enabled.
	virtual bool ManipulationEnabled() const
	{
		return bManipulationEnabled;
	}

	// Sets the manipulatable subject to enabled or disabled
	virtual bool SetManipulationEnabled(bool Enabled = true)
	{
		if (bManipulationEnabled == Enabled)
		{
			return false;
		}
		bManipulationEnabled = Enabled;
		return true;
	}

	// Returns a list of available spaces on the subject
	virtual const TArray<FRigSpace>& AvailableSpaces() const = 0;

	// Returns a space given its name
	virtual FRigSpace* FindSpace(const FName& InSpaceName) = 0;

	// Gets a space's transform given a global / world transform.
	virtual FTransform GetSpaceGlobalTransform(const FName& InSpaceName) = 0;

	// Sets a space's transform given a global / world transform.
	// Returns true when successful.
	virtual bool SetSpaceGlobalTransform(const FName& InSpaceName, const FTransform& InTransform) = 0;

	// Returns a list of available controls on the subject.
	// Each control provides additional information such as metadata,
	// it's value type and so on.
	virtual const TArray<FRigControl>& AvailableControls() const = 0;

	// Returns a control given its name
	virtual FRigControl* FindControl(const FName& InControlName) = 0;

	// Returns the value of a Control
	FORCEINLINE const FRigControlValue& GetControlValue(const FName& InControlName)
	{
		const FRigControl* Control = FindControl(InControlName);
		check(Control);
		return Control->Value;
	}

	// Sets the relative value of a Control
	FORCEINLINE void SetControlValue(const FName& InControlName, const FRigControlValue& InValue, bool bNotify = true)
	{
		FRigControl* Control = FindControl(InControlName);
		check(Control);
		Control->Value = InValue;

		if (bNotify && OnControlModified.IsBound())
		{
			OnControlModified.Broadcast(this, *Control);
		}
	}

	// Sets the relative value of a Control
	template<class T>
	FORCEINLINE void SetControlValue(const FName& InControlName, T InValue, bool bNotify = true)
	{
		SetControlValue(InControlName, FRigControlValue::Make<T>(InValue), bNotify);
	}

	// Returns the global / world transform of a Control
	virtual FTransform GetControlGlobalTransform(const FName& InControlName) const = 0;

	// Sets the global / world transform of a Control. This should be called from the interaction
	// layer / edit mode. Returns true when successful.
	bool SetControlGlobalTransform(const FName& InControlName, const FTransform& InGlobalTransform);

	// Returns the value given a global transform
	virtual FRigControlValue GetControlValueFromGlobalTransform(const FName& InControlName, const FTransform& InGlobalTransform) = 0;

	// Sets a Control's Space (for space switching), returns true when successful.
	virtual bool SetControlSpace(const FName& InControlName, const FName& InSpaceName) = 0;

	// Returns a event that can be used to subscribe to
	// filtering control data when needed
	FFilterControlEvent& ControlFilter() { return OnFilterControl;  }
	
	// Returns a event that can be used to subscribe to
	// change notifications coming from the manipulated subject.
	FControlModifiedEvent& ControlModified() { return OnControlModified;  }

#if WITH_EDITOR
	// Returns a event that can be used to subscribe to
	// selection changes coming from the manipulated subject.
	FControlSelectedEvent& ControlSelected() { return OnControlSelected; }

	//Select or deselected the specified control  
	virtual void SelectControl(const FName& InControlName, bool bSelect = true) = 0;

	//Clear selection on all controls
	virtual bool ClearControlSelection() = 0;

	//Get the current selection
	virtual TArray<FName> CurrentControlSelection() const = 0;

	//Is the Specified Control Selected
	virtual bool IsControlSelected(const FName& InControlName) const = 0;

#endif
	//Get Name
	virtual FString GetName() const = 0;

	// Returns the gizmo library used for generating gizmos
	virtual UControlRigGizmoLibrary* GetGizmoLibrary() const { return nullptr; }

	//Set Object Binding
	virtual void SetObjectBinding(TSharedPtr<IControlRigObjectBinding> InObjectBinding) {};

	//Get bindings to a runtime object 
	virtual TSharedPtr<IControlRigObjectBinding> GetObjectBinding() const {	return nullptr;}


private:

	FFilterControlEvent OnFilterControl;
	FControlModifiedEvent OnControlModified;
#if WITH_EDITOR
	FControlSelectedEvent OnControlSelected;
#endif
	/** True if currently manipulation is enabled */
	bool bManipulationEnabled;
};