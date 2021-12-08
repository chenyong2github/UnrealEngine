// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "../ConsoleVariablesEditorListRow.h"

class IConsoleVariablesEditorListFilter
{
public:
	
	virtual ~IConsoleVariablesEditorListFilter() = default;

	virtual FString GetFilterName() = 0;

	/** Returns localized text to display for when this filter' active state is defined by a toggle button. */
	virtual FText GetFilterButtonLabel() = 0;
	/** Returns localized text to display when the user mouses over a toggle button that defines whether this filter is active. */
	virtual FText GetFilterButtonToolTip() = 0;
	
	void SetFilterActive(const bool bNewEnabled)
	{
		bIsFilterActive = bNewEnabled;
	}

	void ToggleFilterActive()
	{
		bIsFilterActive = !bIsFilterActive;
	}

	bool GetIsFilterActive() const
	{
		return bIsFilterActive;
	}

	virtual bool DoesItemPassFilter(const FConsoleVariablesEditorListRowPtr& InItem)
	{
		return false;
	}

private:

	bool bIsFilterActive = true;
			
};
