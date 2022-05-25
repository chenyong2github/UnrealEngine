// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVM/CurveEditorIntegrationExtension.h"
#include "MVVM/TrackModelStorageExtension.h"
#include "MVVM/ViewModels/ViewModel.h"
#include "MVVM/SharedViewModelData.h"
#include "MVVM/ViewModelPtr.h"
#include "MVVM/ViewModels/SequenceModel.h"
#include "MVVM/ViewModels/ViewModelIterators.h"
#include "MVVM/Extensions/ICurveEditorTreeItemExtension.h"
#include "CurveEditor.h"

namespace UE
{
namespace Sequencer
{

FCurveEditorIntegrationExtension::FCurveEditorIntegrationExtension()
{
}

void FCurveEditorIntegrationExtension::OnCreated(TSharedRef<FViewModel> InWeakOwner)
{
	ensureMsgf(!WeakOwnerModel.Pin().IsValid(), TEXT("This extension was already created!"));
	WeakOwnerModel = InWeakOwner->CastThisShared<FSequenceModel>();

	FSimpleMulticastDelegate& HierarchyChanged = InWeakOwner->GetSharedData()->SubscribeToHierarchyChanged(InWeakOwner);
	HierarchyChanged.AddSP(this, &FCurveEditorIntegrationExtension::OnHierarchyChanged);
}

void FCurveEditorIntegrationExtension::OnHierarchyChanged()
{
	UpdateCurveEditor();
}

void FCurveEditorIntegrationExtension::UpdateCurveEditor()
{
	TSharedPtr<FSequenceModel> OwnerModel = WeakOwnerModel.Pin();
	if (!OwnerModel)
	{
		return;
	}

	ICurveEditorExtension* CurveEditorExtension = OwnerModel->CastThis<ICurveEditorExtension>();
	if (!CurveEditorExtension)
	{
		return;
	}

	TSharedPtr<FCurveEditor> CurveEditor = CurveEditorExtension->GetCurveEditor();
	if (!CurveEditor)
	{
		return;
	}

	FCurveEditorTree* CurveEditorTree = CurveEditor->GetTree();

	// Guard against multiple broadcasts here and defer them until the end of this function
	FScopedCurveEditorTreeEventGuard ScopedEventGuard = CurveEditorTree->ScopedEventGuard();

	// Remove any stale tree items
	for (auto It = ViewModelToTreeItemIDMap.CreateIterator(); It; ++It)
	{
		bool bIsRelevant = false;
		const FCurveEditorTreeItemID ItemID(It.Value());
		TViewModelPtr<ICurveEditorTreeItemExtension> ViewModel(It.Key().Pin());
		if (ViewModel)
		{
			TViewModelPtr<IOutlinerExtension> OutlinerModel = ViewModel.ImplicitCast();
			const bool bIsVisible = OutlinerModel && !OutlinerModel->IsFilteredOut();
			
			const FCurveEditorTreeItem* TreeItem = CurveEditorTree->FindItem(ItemID);

			FCurveEditorTreeItemID ParentID;
			if (auto Parent = ViewModel.AsModel()->FindAncestorOfType<ICurveEditorTreeItemExtension>())
			{
				ParentID = ViewModelToTreeItemIDMap.FindRef(Parent.AsModel());
			}
			
			bIsRelevant = bIsVisible && TreeItem && TreeItem->GetParentID() == ParentID;
		}
		
		if (!bIsRelevant)
		{
			if (ViewModel)
			{
				ViewModel->OnRemovedFromCurveEditor(CurveEditor);
			}
			CurveEditor->RemoveTreeItem(ItemID);
			It.RemoveCurrent();
		}
	}

	// Do a second pass to remove any items that were removed recursively above
	for (auto It = ViewModelToTreeItemIDMap.CreateIterator(); It; ++It)
	{
		if (CurveEditorTree->FindItem(It->Value) == nullptr)
		{
			It.RemoveCurrent();
		}
	}

	// Iterate all non-filtered out outliners and check for curve editor tree extensions
	const bool bIncludeRootNode = false;
	for (TParentFirstChildIterator<IOutlinerExtension> It(OwnerModel, bIncludeRootNode); It; ++It)
	{
		if (It->IsFilteredOut())
		{
			It.IgnoreCurrentChildren();
			continue;
		}

		if (TViewModelPtr<ICurveEditorTreeItemExtension> ChildViewModel = (*It).ImplicitCast())
		{
			const FCurveEditorTreeItemID ItemID = ViewModelToTreeItemIDMap.FindRef(ChildViewModel.AsModel());
			if (!ItemID.IsValid())
			{
				AddToCurveEditor(ChildViewModel.AsModel(), CurveEditor);
			}
		}
	}
}

FCurveEditorTreeItemID FCurveEditorIntegrationExtension::AddToCurveEditor(TViewModelPtr<ICurveEditorTreeItemExtension> InViewModel, TSharedPtr<FCurveEditor> CurveEditor)
{
	// If the view model doesn't want to be in the curve editor, bail out
	// Note that this means we will create curve editor items for each parent in the hierarchy up
	// until the first parent that doesn't implement ICurveEditorTreeItemExtension
	// That is: we don't create "dummy" entries when there's a "gap" in the hierarchy
	if (!InViewModel)
	{
		return FCurveEditorTreeItemID::Invalid();
	}

	// Check if we already have a valid curve editor ID
	if (FCurveEditorTreeItemID* Existing = ViewModelToTreeItemIDMap.Find(InViewModel.AsModel()))
	{
		if (CurveEditor->GetTree()->FindItem(*Existing) != nullptr)
		{
			return *Existing;
		}
	}

	// Recursively create any needed parent curve editor items
	TSharedPtr<FViewModel> Parent = InViewModel.AsModel()->GetParent();

	FCurveEditorTreeItemID ParentID = Parent.IsValid() ? 
		AddToCurveEditor(Parent, CurveEditor) : 
		FCurveEditorTreeItemID::Invalid();

	// Create the new curve editor item
	FCurveEditorTreeItem* NewItem = CurveEditor->AddTreeItem(ParentID);
	TSharedPtr<ICurveEditorTreeItem> CurveEditorTreeItem = InViewModel->GetCurveEditorTreeItem();
	NewItem->SetWeakItem(CurveEditorTreeItem);

	// Register the new ID in our map and notify the view model
	ViewModelToTreeItemIDMap.Add(InViewModel.AsModel(), NewItem->GetID());
	InViewModel->OnAddedToCurveEditor(NewItem->GetID(), CurveEditor);

	return NewItem->GetID();
}

void FCurveEditorIntegrationExtension::RecreateCurveEditor()
{
	TSharedPtr<FSequenceModel> OwnerModel = WeakOwnerModel.Pin();
	if (!OwnerModel)
	{
		return;
	}

	ICurveEditorExtension* CurveEditorExtension = OwnerModel->CastThis<ICurveEditorExtension>();
	if (!CurveEditorExtension)
	{
		return;
	}

	TSharedPtr<FCurveEditor> CurveEditor = CurveEditorExtension->GetCurveEditor();
	if (!CurveEditor)
	{
		return;
	}

	for (auto Pair : ViewModelToTreeItemIDMap)
	{
		FCurveEditorTreeItemID ItemID(Pair.Value);
		TViewModelPtr<ICurveEditorTreeItemExtension> ViewModel(Pair.Key.Pin());
		if (ViewModel)
		{
			ViewModel->OnRemovedFromCurveEditor(CurveEditor);
		}
		CurveEditor->RemoveTreeItem(ItemID);
	}

	ViewModelToTreeItemIDMap.Reset();
}

} // namespace Sequencer
} // namespace UE

