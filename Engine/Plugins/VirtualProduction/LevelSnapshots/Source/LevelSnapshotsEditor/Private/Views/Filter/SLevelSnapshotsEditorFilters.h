// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailsView.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

class FLevelSnapshotsEditorFilters;
class SFavoriteFilterList;
class SMasterFilterIndicatorButton;
class SCustomSplitter;
class ULevelSnapshotsEditorData;
class ULevelSnapshotFilter;
class UConjunctionFilter;

enum class EFilterBehavior : uint8;

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
	TSharedPtr<FLevelSnapshotsEditorFilters> GetFiltersModel() const;
	const TSharedPtr<IDetailsView>& GetFilterDetailsView() const;
	bool IsResizingDetailsView() const;
	
	void RemoveFilter(UConjunctionFilter* FilterToRemove);

private:

	FReply OnClickUpdateResultsView();
	
	/** Generates a tree row. */
	TSharedRef<ITableRow> OnGenerateRow(UConjunctionFilter* InManagedFilter, const TSharedRef<STableViewBase>& OwnerTable);
	/** Generate the groups using the preset's layout data. */
	void RefreshGroups();

	FReply AddFilterClick();

	FDelegateHandle OnUserDefinedFiltersChangedHandle;
	FDelegateHandle OnEditedFilterChangedHandle;
	
	TSharedPtr<SFavoriteFilterList> FavoriteList;
	TSharedPtr<STreeView<UConjunctionFilter*>> FilterRowsList;

	/** Filter input details view */
	TSharedPtr<IDetailsView> FilterDetailsView;
	/* Splits filters and details panel */
	TSharedPtr<SCustomSplitter> DetailsSplitter;

	TWeakPtr<FLevelSnapshotsEditorFilters> FiltersModelPtr;
	TWeakObjectPtr<ULevelSnapshotsEditorData> EditorDataPtr;
};
