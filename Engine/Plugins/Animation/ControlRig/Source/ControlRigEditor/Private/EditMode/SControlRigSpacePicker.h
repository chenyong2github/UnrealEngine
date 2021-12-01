// Copyright Epic Games, Inc. All Rights Reserved.

/**
* Space Picker View
*/
#pragma once

#include "CoreMinimal.h"
#include "ControlRigBaseDockableView.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Misc/FrameNumber.h"
#include "ControlRig.h"
#include "SRigSpacePickerWidget.h"


class ISequencer;
class SExpandableArea;
class SSearchableRigHierarchyTreeView;

class SControlRigSpacePicker : public SCompoundWidget, public FControlRigBaseDockableView
{

	SLATE_BEGIN_ARGS(SControlRigSpacePicker)
	{}
	SLATE_END_ARGS()
		~SControlRigSpacePicker();

	void Construct(const FArguments& InArgs, FControlRigEditMode& InEditMode);

private:
	virtual void HandleControlSelected(UControlRig* Subject, FRigControlElement* InControl, bool bSelected) override;
	virtual void HandleControlAdded(UControlRig* ControlRig, bool bIsAdded) override;
	virtual void NewControlRigSet(UControlRig* ControlRig) override;

	const URigHierarchy* GetHierarchy() const;
	void HandleSelectionChanged(TSharedPtr<FRigTreeElement> Selection, ESelectInfo::Type SelectInfo);

	/** Space picker widget*/
	TSharedPtr<SRigSpacePickerWidget> SpacePickerWidget;
	TSharedPtr<SExpandableArea> PickerExpander;

	const FRigControlElementCustomization* HandleGetControlElementCustomization(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey);
	void HandleActiveSpaceChanged(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey, const FRigElementKey& InSpaceKey);
	void HandleSpaceListChanged(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey, const TArray<FRigElementKey>& InSpaceList);
	FReply HandleAddSpaceClicked();
	FReply OnBakeControlsToNewSpaceButtonClicked();

};

