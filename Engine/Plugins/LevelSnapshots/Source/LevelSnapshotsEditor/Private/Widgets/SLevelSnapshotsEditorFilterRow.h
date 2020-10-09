// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SSearchBox;

class SLevelSnapshotsEditorFilterRow : public SCompoundWidget
{
public:
	~SLevelSnapshotsEditorFilterRow();

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorFilterRow)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	/** Makes the filters menu */
	TSharedRef<SWidget> MakeAddFilterMenu();

	/** Called when reset filters option is pressed */
	void OnResetFilters();

	void CreateFiltersMenuCategory(FMenuBuilder& MenuBuilder, TArray<FText> AdvancedFilter);

	void FilterByTypeCategoryClicked(FText ParentCategory);

	bool IsFilterTypeCategoryInUse(FText ParentCategory);


private:
	TSharedPtr<SSearchBox> SearchBox;
};

