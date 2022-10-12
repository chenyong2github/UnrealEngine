// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/List/ObjectMixerEditorList.h"

#include "Views/List/SObjectMixerEditorList.h"

FObjectMixerEditorList::FObjectMixerEditorList(TSharedRef<FObjectMixerEditorMainPanel, ESPMode::ThreadSafe> InMainPanel)
{
	MainPanelModelPtr = InMainPanel;

	InMainPanel->OnPreFilterChange.AddRaw(this, &FObjectMixerEditorList::OnPreFilterChange);
	InMainPanel->OnPostFilterChange.AddRaw(this, &FObjectMixerEditorList::OnPostFilterChange);
}

FObjectMixerEditorList::~FObjectMixerEditorList()
{
	FlushWidget();
}

void FObjectMixerEditorList::FlushWidget()
{
	if (TSharedPtr<FObjectMixerEditorMainPanel> MainPanelPinned = MainPanelModelPtr.Pin())
	{
		MainPanelPinned->OnPreFilterChange.RemoveAll(this);
		MainPanelPinned->OnPostFilterChange.RemoveAll(this);
	}
	
	ListWidget.Reset();
}

TSharedRef<SWidget> FObjectMixerEditorList::GetOrCreateWidget()
{
	if (!ListWidget.IsValid())
	{
		SAssignNew(ListWidget, SObjectMixerEditorList, SharedThis(this));
	}

	RequestRebuildList();

	return ListWidget.ToSharedRef();
}

void FObjectMixerEditorList::OnPreFilterChange()
{
	if (ListWidget.IsValid())
	{
		if (const TSharedPtr<FObjectMixerEditorMainPanel> PinnedMainPanel = GetMainPanelModel().Pin())
		{		
			ListWidget->CacheTreeState(PinnedMainPanel->GetWeakActiveListFiltersSortedByName());
		}
	}
}

void FObjectMixerEditorList::OnPostFilterChange()
{
	if (ListWidget.IsValid())
	{
		if (const TSharedPtr<FObjectMixerEditorMainPanel> PinnedMainPanel = GetMainPanelModel().Pin())
		{		
			ListWidget->EvaluateIfRowsPassFilters();
			ListWidget->RestoreTreeState(PinnedMainPanel->GetWeakActiveListFiltersSortedByName());
		}
	}
}

void FObjectMixerEditorList::ClearList() const
{
	if (ListWidget.IsValid())
	{
		ListWidget->ClearList();
	}
}

void FObjectMixerEditorList::RequestRebuildList() const
{
	if (ListWidget.IsValid())
	{
		ListWidget->RequestRebuildList();
	}
}

void FObjectMixerEditorList::RefreshList() const
{
	if (ListWidget.IsValid())
	{
		ListWidget->RefreshList();
	}
}

void FObjectMixerEditorList::RequestSyncEditorSelectionToListSelection() const
{
	if (ListWidget.IsValid())
	{
		ListWidget->RequestSyncEditorSelectionToListSelection();
	}
}

void FObjectMixerEditorList::ExecuteListViewSearchOnAllRows(const FString& SearchString,
                                                            const bool bShouldRefreshAfterward)
{
	if (ListWidget.IsValid())
	{
		ListWidget->ExecuteListViewSearchOnAllRows(SearchString, bShouldRefreshAfterward);
	}
}

void FObjectMixerEditorList::EvaluateIfRowsPassFilters(const bool bShouldRefreshAfterward) const
{
	if (ListWidget.IsValid())
	{
		ListWidget->EvaluateIfRowsPassFilters();
	}
}

TSet<TWeakPtr<FObjectMixerEditorListRow>> FObjectMixerEditorList::GetSoloRows() const
{
	if (ListWidget.IsValid())
	{
		return ListWidget->GetSoloRows();
	}
	
	return {};
}

void FObjectMixerEditorList::ClearSoloRows()
{
	if (ListWidget.IsValid())
	{
		ListWidget->ClearSoloRows();
	}
}

bool FObjectMixerEditorList::IsListInSoloState() const
{
	if (ListWidget.IsValid())
	{
		return ListWidget->IsListInSoloState();
	}
	
	return false;
}

void FObjectMixerEditorList::EvaluateAndSetEditorVisibilityPerRow()
{
	if (ListWidget.IsValid())
	{
		ListWidget->EvaluateAndSetEditorVisibilityPerRow();
	}
}

TWeakPtr<FObjectMixerEditorMainPanel> FObjectMixerEditorList::GetMainPanelModel()
{
	return MainPanelModelPtr;
}
