// Copyright Epic Games, Inc. All Rights Reserved.
/**
* View for holding ControlRig Animation Outliner
*/
#pragma once

#include "CoreMinimal.h"
#include "ControlRigBaseDockableView.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "ControlRig.h"
#include "Rigs/RigHierarchy.h"
#include "SRigHierarchyTreeView.h"


class ISequencer;
class SExpandableArea;
class SSearchableRigHierarchyTreeView;

class SControlRigOutliner: public SCompoundWidget, public FControlRigBaseDockableView
{

	SLATE_BEGIN_ARGS(SControlRigOutliner)
	{}
	SLATE_END_ARGS()
	~SControlRigOutliner();

	void Construct(const FArguments& InArgs, FControlRigEditMode& InEditMode);

private:
	virtual void HandleControlSelected(UControlRig* Subject, FRigControlElement* InControl, bool bSelected) override;
	virtual void HandleControlAdded(UControlRig* ControlRig, bool bIsAdded) override;
	virtual void NewControlRigSet(UControlRig* ControlRig) override;

	const URigHierarchy* GetHierarchy() const;
	void HandleSelectionChanged(TSharedPtr<FRigTreeElement> Selection, ESelectInfo::Type SelectInfo);

	/** Hierarchy picker for controls*/
	TSharedPtr<SSearchableRigHierarchyTreeView> HierarchyTreeView;
	FRigTreeDisplaySettings DisplaySettings;
	const FRigTreeDisplaySettings& GetDisplaySettings() const { return DisplaySettings; }
	bool bIsChangingRigHierarchy = false;
	TSharedPtr<SExpandableArea> PickerExpander;
};

