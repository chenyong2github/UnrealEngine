// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/List/ObjectMixerEditorList.h"

#include "Views/List/SObjectMixerEditorList.h"

FObjectMixerEditorList::~FObjectMixerEditorList()
{
	FlushWidget();
}

void FObjectMixerEditorList::FlushWidget()
{
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

void FObjectMixerEditorList::ExecuteListViewSearchOnAllRows(const FString& SearchString,
	const bool bShouldRefreshAfterward)
{
	ListWidget->ExecuteListViewSearchOnAllRows(SearchString, bShouldRefreshAfterward);
}

void FObjectMixerEditorList::EvaluateIfRowsPassFilters(const bool bShouldRefreshAfterward) const
{
	ListWidget->EvaluateIfRowsPassFilters();
}

TWeakPtr<FObjectMixerEditorMainPanel> FObjectMixerEditorList::GetMainPanelModel()
{
	return MainPanelModelPtr;
}

TWeakPtr<FObjectMixerEditorListRow> FObjectMixerEditorList::GetSoloRow()
{
	return GetMainPanelModel().Pin()->GetSoloRow();
}

void FObjectMixerEditorList::SetSoloRow(TSharedRef<FObjectMixerEditorListRow> InRow)
{
	GetMainPanelModel().Pin()->SetSoloRow(InRow);
}

void FObjectMixerEditorList::ClearSoloRow()
{
	GetMainPanelModel().Pin()->ClearSoloRow();
}
