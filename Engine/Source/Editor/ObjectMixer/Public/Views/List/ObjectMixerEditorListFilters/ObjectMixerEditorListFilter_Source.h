// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IObjectMixerEditorListFilter.h"

#define LOCTEXT_NAMESPACE "ObjectMixerEditor"

class OBJECTMIXEREDITOR_API FObjectMixerEditorListFilter_Source : public IObjectMixerEditorListFilter
{
public:

	FObjectMixerEditorListFilter_Source(){}

	virtual FString GetFilterName() override
	{
		return "";
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

	virtual bool DoesItemPassFilter(const FObjectMixerEditorListRowPtr& InItem) override
	{
		if (InItem.IsValid())
		{
			
		}

		return false;
	}

};

#undef LOCTEXT_NAMESPACE
