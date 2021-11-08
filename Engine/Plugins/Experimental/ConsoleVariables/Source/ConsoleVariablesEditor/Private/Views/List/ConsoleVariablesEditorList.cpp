// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/List/ConsoleVariablesEditorList.h"

#include "Views/List/SConsoleVariablesEditorList.h"

FConsoleVariablesEditorList::~FConsoleVariablesEditorList()
{
	
}

TSharedRef<SWidget> FConsoleVariablesEditorList::GetOrCreateWidget()
{
	if (!ListWidget.IsValid())
	{
		SAssignNew(ListWidget, SConsoleVariablesEditorList);
	}

	return ListWidget.ToSharedRef();
}

void FConsoleVariablesEditorList::RefreshList(TObjectPtr<UConsoleVariablesAsset> InAsset, const FString& InConsoleCommandToScrollTo) const
{
	if (ListWidget.IsValid())
	{
		ListWidget->RefreshList(InAsset, InConsoleCommandToScrollTo);
	}
}

void FConsoleVariablesEditorList::UpdatePresetValuesForSave(TObjectPtr<UConsoleVariablesAsset> InAsset) const
{
	if (ListWidget.IsValid())
	{
		ListWidget->UpdatePresetValuesForSave(InAsset);
	}
}
