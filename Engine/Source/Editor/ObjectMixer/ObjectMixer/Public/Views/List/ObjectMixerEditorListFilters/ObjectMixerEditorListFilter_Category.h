// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IObjectMixerEditorListFilter.h"

#define LOCTEXT_NAMESPACE "ObjectMixerEditor"

class OBJECTMIXEREDITOR_API FObjectMixerEditorListFilter_Category : public IObjectMixerEditorListFilter
{
public:

	FObjectMixerEditorListFilter_Category()
	{
		SetFilterMatchType(EObjectMixerEditorListFilterMatchType::MatchAll);
	}

	virtual FString GetFilterName() const override
	{
		return "ObjectMixerCategoryListFilter";
	}

	virtual bool IsToggleable() const override
	{
		return false;
	}

	virtual FText GetFilterButtonLabel() const override
	{
		return LOCTEXT("ShowSourceTextFilterFormat", "Show Categories");
	}

	virtual FText GetFilterButtonToolTip() const override
	{
		return LOCTEXT("ShowSourceTextFilterTooltipFormat", "Show rows that are assigned to the selected categories");
	}

	virtual bool DoesItemPassFilter(const FObjectMixerEditorListRowPtr& InItem) const override
	{
		if (InItem.IsValid())
		{
			if (InItem->GetRowType() == FObjectMixerEditorListRow::ContainerObject ||
				InItem->GetRowType() == FObjectMixerEditorListRow::MatchingObject)
			{
				return InItem->IsObjectRefInSelectedCategories();
			}
		}

		return false;
	}

};

#undef LOCTEXT_NAMESPACE
