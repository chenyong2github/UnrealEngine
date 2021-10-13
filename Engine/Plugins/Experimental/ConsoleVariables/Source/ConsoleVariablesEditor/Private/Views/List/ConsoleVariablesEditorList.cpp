// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/List/ConsoleVariablesEditorList.h"

#include "Views/List/SConsoleVariablesEditorList.h"

FConsoleVariablesEditorList::FConsoleVariablesEditorList()
{
	OnCommandEntered = FConsoleCommandDelegate::CreateRaw(this, &FConsoleVariablesEditorList::UpdateExistingValuesFromConsoleManager);
}

FConsoleVariablesEditorList::~FConsoleVariablesEditorList()
{
	OnCommandEntered.Unbind();
}

TSharedRef<SWidget> FConsoleVariablesEditorList::GetOrCreateWidget()
{
	if (!ListWidget.IsValid())
	{
		SAssignNew(ListWidget, SConsoleVariablesEditorList);
	}

	FAutoConsoleVariableSink CVarMySink(OnCommandEntered);

	return ListWidget.ToSharedRef();
}

void FConsoleVariablesEditorList::RefreshList(UConsoleVariablesAsset* InAsset) const
{
	if (ListWidget.IsValid())
	{
		ListWidget->RefreshList(InAsset);
	}
}

void FConsoleVariablesEditorList::UpdateExistingValuesFromConsoleManager() const
{
	if (ListWidget.IsValid())
	{
		ListWidget->UpdateExistingValuesFromConsoleManager();
	}
}
