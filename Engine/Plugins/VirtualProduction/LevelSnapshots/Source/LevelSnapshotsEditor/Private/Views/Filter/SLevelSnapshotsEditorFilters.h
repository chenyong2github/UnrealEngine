// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FLevelSnapshotsEditorFilters;
class SFavoriteFilterList;
class ULevelSnapshotsEditorData;
class ULevelSnapshotFilter;
class UConjunctionFilter;

/* Contents of filters tab */
class SLevelSnapshotsEditorFilters : public SCompoundWidget
{
public:

	~SLevelSnapshotsEditorFilters();

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorFilters)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorFilters>& InFilters);

	ULevelSnapshotsEditorData* GetEditorData() const;

	TSharedRef<FLevelSnapshotsEditorFilters> GetFiltersModel() const;
	
	void RemoveFilter(UConjunctionFilter* FilterToRemove);

private:
	
	/** Generates a tree row. */
	TSharedRef<ITableRow> OnGenerateRow(UConjunctionFilter* InManagedFilter, const TSharedRef<STableViewBase>& OwnerTable);
	/** Generate the groups using the preset's layout data. */
	void RefreshGroups();

	FReply AddFilterClick();

	FDelegateHandle OnUserDefinedFiltersChangedHandle;
	
	TSharedPtr<SFavoriteFilterList> FavoriteList;
	TSharedPtr<STreeView<UConjunctionFilter*>> FilterRowsList;

	/** Filter input details view */
	TSharedPtr<IDetailsView> FilterInputDetailsView;

	TWeakPtr<FLevelSnapshotsEditorFilters> FiltersModelPtr;
};
