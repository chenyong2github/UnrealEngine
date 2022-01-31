// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IConsoleVariablesEditorListFilter.h"

#define LOCTEXT_NAMESPACE "ConsoleVariablesEditor"

class FConsoleVariablesEditorListFilter_SourceText : public IConsoleVariablesEditorListFilter
{
public:

	FConsoleVariablesEditorListFilter_SourceText(const FString& InFilterString)
	: FilterString(InFilterString){}

	virtual FString GetFilterName() override
	{
		return FilterString;
	}

	virtual FText GetFilterButtonLabel() override
	{
		return FText::Format(LOCTEXT("ShowSourceTextFilterFormat", "Show {0}"), FText::FromString(GetFilterName()));
	}

	virtual FText GetFilterButtonToolTip() override
	{
		return FText::Format(
			LOCTEXT("ShowSourceTextFilterTooltipFormat", "Show rows that have a Source field matching '{0}'"),
			FText::FromString(GetFilterName()));
	}

	virtual bool DoesItemPassFilter(const FConsoleVariablesEditorListRowPtr& InItem) override
	{
		if (InItem.IsValid(); const TSharedPtr<FConsoleVariablesEditorCommandInfo> PinnedCommand = InItem->GetCommandInfo().Pin())
		{
			const FString& StringToSearch = PinnedCommand->GetSourceAsText().ToString();

			return StringToSearch.Contains(FilterString);
		}

		return false;
	}
	
private:

	FString FilterString;
};

#undef LOCTEXT_NAMESPACE
