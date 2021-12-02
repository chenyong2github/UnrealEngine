// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SWidget.h"

struct FConsoleVariablesEditorListRow;
typedef TSharedPtr<FConsoleVariablesEditorListRow> FConsoleVariablesEditorListRowPtr;

class SConsoleVariablesEditorList;
class UConsoleVariablesAsset;

class FConsoleVariablesEditorList : public TSharedFromThis<FConsoleVariablesEditorList>
{
public:

	FConsoleVariablesEditorList(){};

	~FConsoleVariablesEditorList();

	TSharedRef<SWidget> GetOrCreateWidget();

	void RefreshList(const FString& InConsoleCommandToScrollTo = "") const;

	/** Updates the saved values in a UConsoleVariablesAsset so that the command/value map can be saved to disk */
	void UpdatePresetValuesForSave(TObjectPtr<UConsoleVariablesAsset> InAsset) const;

private:

	TSharedPtr<SConsoleVariablesEditorList> ListWidget;
};
