// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/MainPanel/ObjectMixerEditorMainPanel.h"

#include "ObjectMixerEditorModule.h"
#include "ObjectMixerEditorProjectSettings.h"
#include "Views/List/ObjectMixerEditorList.h"
#include "Views/MainPanel/SObjectMixerEditorMainPanel.h"

FObjectMixerEditorMainPanel::FObjectMixerEditorMainPanel()
{
	EditorList = MakeShared<FObjectMixerEditorList>();
}

TSharedRef<SWidget> FObjectMixerEditorMainPanel::GetOrCreateWidget()
{
	if (!MainPanelWidget.IsValid())
	{
		SAssignNew(MainPanelWidget, SObjectMixerEditorMainPanel, SharedThis(this));
	}

	return MainPanelWidget.ToSharedRef();
}

void FObjectMixerEditorMainPanel::RebuildList(const FString InItemToScrollTo, bool bShouldCacheValues) const
{
	if (EditorList.IsValid())
	{
		EditorList->RebuildList(InItemToScrollTo);
	}
}

void FObjectMixerEditorMainPanel::RefreshList() const
{
	if (EditorList.IsValid())
	{
		EditorList->RefreshList();
	}
}

void FObjectMixerEditorMainPanel::OnClassSelectionChanged(UClass* InNewClass) const
{
	if (TSharedPtr<FObjectMixerEditorList, ESPMode::ThreadSafe> PinnedList = GetEditorList().Pin())
	{
		PinnedList->SetObjectFilterClass(InNewClass);
	}
}

TObjectPtr<UClass> FObjectMixerEditorMainPanel::GetClassSelection() const
{
	if (TSharedPtr<FObjectMixerEditorList, ESPMode::ThreadSafe> PinnedList = GetEditorList().Pin())
	{
		return PinnedList->GetObjectFilterClass();
	}

	return nullptr;
}

bool FObjectMixerEditorMainPanel::IsClassSelected(UClass* InNewClass) const
{
	return InNewClass == GetClassSelection();
}
