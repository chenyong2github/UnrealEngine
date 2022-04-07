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
#include "Widgets/SBoxPanel.h"


class ISequencer;
class SExpandableArea;
class SSearchableRigHierarchyTreeView;
class UControlRig;

class SControlRigOutlinerItem : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SControlRigOutlinerItem){}
	SLATE_ARGUMENT(UControlRig*, ControlRig)
	SLATE_ARGUMENT(FControlRigEditMode*, EditMode)
	SLATE_END_ARGS()
	SControlRigOutlinerItem();
	~SControlRigOutlinerItem();

	void Construct(const FArguments& InArgs);
private:
	void HandleControlSelected(UControlRig* Subject, FRigControlElement* InControl, bool bSelected);
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& OldToNewInstanceMap);
	void NewControlRigSet(UControlRig* ControlRig);

	const URigHierarchy* GetHierarchy() const;
	void HandleSelectionChanged(TSharedPtr<FRigTreeElement> Selection, ESelectInfo::Type SelectInfo);

	//visibility button
	const FSlateBrush* GetVisibilityBrushForElement() const;
	FReply OnToggleVisibility();
	bool VisibilityToggleEnabled() const;


	/** Hierarchy picker for controls*/
	TSharedPtr<SSearchableRigHierarchyTreeView> HierarchyTreeView;
	FRigTreeDisplaySettings DisplaySettings;
	const FRigTreeDisplaySettings& GetDisplaySettings() const { return DisplaySettings; }
	bool bIsChangingRigHierarchy = false;
	TSharedPtr<SExpandableArea> PickerExpander;

	TWeakObjectPtr<UControlRig> CurrentControlRig;
	FControlRigEditMode* ControlRigEditMode = nullptr;

};

class SControlRigOutliner: public SCompoundWidget
{

	SLATE_BEGIN_ARGS(SControlRigOutliner)
	{}
	SLATE_END_ARGS()
	~SControlRigOutliner();

	void Construct(const FArguments& InArgs, FControlRigEditMode& InEditMode);
	void SetEditMode(FControlRigEditMode& InEditMode);

private:
	void HandleControlAdded(UControlRig* ControlRig, bool bIsAdded);
	void Rebuild();
	TSharedPtr<SVerticalBox> MainBoxPtr;
	FEditorModeTools* ModeTools = nullptr;

};

