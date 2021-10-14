// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SWidget.h"

class UConsoleVariablesAsset;
class SConsoleVariablesEditorList;

class FConsoleVariablesEditorList : public TSharedFromThis<FConsoleVariablesEditorList>
{
public:

	FConsoleVariablesEditorList();

	~FConsoleVariablesEditorList();

	TSharedRef<SWidget> GetOrCreateWidget();

	void RefreshList(UConsoleVariablesAsset* InAsset) const;

	void UpdateExistingValuesFromConsoleManager() const;

private:

	TSharedPtr<SConsoleVariablesEditorList> ListWidget;

	FConsoleCommandDelegate OnCommandEntered;
};
