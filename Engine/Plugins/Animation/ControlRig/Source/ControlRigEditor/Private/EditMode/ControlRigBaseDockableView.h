// Copyright Epic Games, Inc. All Rights Reserved.
/**
* Base View for Dockable Control Rig Animation widgets Details/Outliner
*/
#pragma once

#include "CoreMinimal.h"
#include "EditorModeManager.h"


class UControlRig;
class ISequencer;
class FControlRigEditMode;
struct FRigControlElement;

class FControlRigBaseDockableView 
{
public:
	FControlRigBaseDockableView();
	virtual ~FControlRigBaseDockableView();

	void SetEditMode(FControlRigEditMode& InEditMode);

protected:
	virtual void HandleControlSelected(UControlRig* Subject, FRigControlElement* InControl, bool bSelected);
	virtual void HandleControlAdded(UControlRig* ControlRig, bool bIsAdded);
	virtual void NewControlRigSet(UControlRig* ControlRig);

	void OnObjectsReplaced(const TMap<UObject*, UObject*>& OldToNewInstanceMap);

	UControlRig* GetControlRig();
	ISequencer* GetSequencer() const;
	TWeakObjectPtr<UControlRig> CurrentControlRig;

	FEditorModeTools* ModeTools = nullptr;

};

