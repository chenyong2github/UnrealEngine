// Copyright Epic Games, Inc. All Rights Reserved.

#include "SControlRigOutliner.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "AssetData.h"
#include "EditorStyleSet.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "EditorStyleSet.h"
#include "Styling/CoreStyle.h"
#include "ScopedTransaction.h"
#include "ControlRig.h"
#include "UnrealEdGlobals.h"
#include "ControlRigEditMode.h"
#include "EditorModeManager.h"
#include "ISequencer.h"
#include "LevelSequence.h"
#include "UnrealEd/Public/Selection.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "ControlRigControlsProxy.h"
#include "ControlRigEditModeSettings.h"
#include "TimerManager.h"
#include "SRigHierarchyTreeView.h"
#include "Rigs/RigHierarchyController.h"

#define LOCTEXT_NAMESPACE "ControlRigOutliner"

void SControlRigOutliner::Construct(const FArguments& InArgs, FControlRigEditMode& InEditMode)
{
	bIsChangingRigHierarchy = false;

	DisplaySettings.bShowBones = false;
	DisplaySettings.bShowControls = true;
	DisplaySettings.bShowNulls = false;
	DisplaySettings.bShowReferences = false;
	DisplaySettings.bShowRigidBodies = false;
	DisplaySettings.bHideParentsOnFilter = true;
	DisplaySettings.bFlattenHierarchyOnFilter = true;

	FRigTreeDelegates RigTreeDelegates;
	RigTreeDelegates.OnGetHierarchy = FOnGetRigTreeHierarchy::CreateSP(this, &SControlRigOutliner::GetHierarchy);
	RigTreeDelegates.OnGetDisplaySettings = FOnGetRigTreeDisplaySettings::CreateSP(this, &SControlRigOutliner::GetDisplaySettings);
	RigTreeDelegates.OnSelectionChanged = FOnRigTreeSelectionChanged::CreateSP(this, &SControlRigOutliner::HandleSelectionChanged);

	ChildSlot
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(PickerExpander, SExpandableArea)
					.InitiallyCollapsed(false)
					.AreaTitle(LOCTEXT("Picker_Header", "Controls"))
					.AreaTitleFont(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
					.BorderBackgroundColor(FLinearColor(.6f, .6f, .6f))
					.BodyContent()
					[
						SAssignNew(HierarchyTreeView, SSearchableRigHierarchyTreeView)
						.RigTreeDelegates(RigTreeDelegates)
					]
				]
		
			]
		];

	SetEditMode(InEditMode);
	HierarchyTreeView->GetTreeView()->RefreshTreeView(true);
}

SControlRigOutliner::~SControlRigOutliner()
{
	//base class handles control rig related cleanup
}


void SControlRigOutliner::HandleControlAdded(UControlRig* ControlRig, bool bIsAdded)
{
	FControlRigBaseDockableView::HandleControlAdded(ControlRig, bIsAdded);
	HierarchyTreeView->GetTreeView()->RefreshTreeView(true);
}

void SControlRigOutliner::NewControlRigSet(UControlRig* ControlRig)
{
	FControlRigBaseDockableView::NewControlRigSet(ControlRig);
	HierarchyTreeView->GetTreeView()->RefreshTreeView(true);
}
void SControlRigOutliner::HandleControlSelected(UControlRig* Subject, FRigControlElement* ControlElement, bool bSelected)
{
	FControlRigBaseDockableView::HandleControlSelected(Subject, ControlElement, bSelected);
	const FRigElementKey Key = ControlElement->GetKey();
	for (int32 RootIndex = 0; RootIndex < HierarchyTreeView->GetTreeView()->GetRootElements().Num(); ++RootIndex)
	{
		TSharedPtr<FRigTreeElement> Found = HierarchyTreeView->GetTreeView()->FindElement(Key, HierarchyTreeView->GetTreeView()->GetRootElements()[RootIndex]);
		if (Found.IsValid())
		{
			HierarchyTreeView->GetTreeView()->SetItemSelection(Found, bSelected, ESelectInfo::OnNavigation);

			TArray<TSharedPtr<FRigTreeElement>> SelectedItems = HierarchyTreeView->GetTreeView()->GetSelectedItems();
			for (TSharedPtr<FRigTreeElement> SelectedItem : SelectedItems)
			{
				HierarchyTreeView->GetTreeView()->SetExpansionRecursive(SelectedItem, false, true);
			}

			if (SelectedItems.Num() > 0)
			{
				HierarchyTreeView->GetTreeView()->RequestScrollIntoView(SelectedItems.Last());
			}
		}
	}
}

const URigHierarchy* SControlRigOutliner::GetHierarchy() const
{
	if (CurrentControlRig.IsValid())
	{
		return CurrentControlRig.Get()->GetHierarchy();
	}
	return nullptr;
}

void SControlRigOutliner::HandleSelectionChanged(TSharedPtr<FRigTreeElement> Selection, ESelectInfo::Type SelectInfo)
{
	if (bIsChangingRigHierarchy)
	{
		return;
	}

	const URigHierarchy* Hierarchy = GetHierarchy();
	if (Hierarchy)
	{
		URigHierarchyController* Controller = ((URigHierarchy*)Hierarchy)->GetController(true);
		check(Controller);

		TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);

		const TArray<FRigElementKey> NewSelection = HierarchyTreeView->GetTreeView()->GetSelectedKeys();
		if (!Controller->SetSelection(NewSelection))
		{
			return;
		}
	}
}


#undef LOCTEXT_NAMESPACE
