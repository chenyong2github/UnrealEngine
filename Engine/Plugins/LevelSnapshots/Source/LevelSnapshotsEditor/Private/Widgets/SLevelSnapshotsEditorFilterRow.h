// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

struct FLevelSnapshotsEditorFilterRowGroup;
class SSearchBox;
class SLevelSnapshotsEditorFilters;
class SLevelSnapshotsEditorFilterList;
class ULevelSnapshotFilter;

class SLevelSnapshotsEditorFilterRow : public SCompoundWidget
{
public:
	~SLevelSnapshotsEditorFilterRow();

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorFilterRow)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<SLevelSnapshotsEditorFilters>& InEditorFilters, const TSharedRef<FLevelSnapshotsEditorFilterRowGroup>& InFieldGroup);

private:
	/** Makes the filters menu */
	TSharedRef<SWidget> MakeAddFilterMenu();

	/** Called when reset filters option is pressed */
	void OnAddFilter(TSubclassOf<ULevelSnapshotFilter> InClass, FName InName);

	void CreateFiltersMenuCategory(FMenuBuilder& MenuBuilder, TArray<TPair<UClass*, TSharedPtr<FLevelSnapshotsEditorFilterClass>>> Filters);

	void FilterByTypeCategoryClicked(FText ParentCategory);

	bool IsFilterTypeCategoryInUse(FText ParentCategory);

	FReply RemoveFilter();

private:
	TSharedPtr<SSearchBox> SearchBox;

	TWeakPtr<SLevelSnapshotsEditorFilters> EditorFiltersPtr;

	TSharedPtr<SLevelSnapshotsEditorFilterList> FilterList;

	TWeakPtr<FLevelSnapshotsEditorFilterRowGroup> FieldGroupPtr;
};

