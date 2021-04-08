// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FLevelSnapshotsEditorFilters;
class SCreateNewFilterWidget;
class SLevelSnapshotsEditorFilter;
class SWrapBox;
class UConjunctionFilter;
class UNegatableFilter;

/* Lists a bunch of filters. */
class SLevelSnapshotsEditorFilterList : public SCompoundWidget
{
public:
	
	~SLevelSnapshotsEditorFilterList();

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorFilterList)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UConjunctionFilter* InManagedAndCondition, const TSharedRef<FLevelSnapshotsEditorFilters>& InEditorFilterModel);

private:
	
	void OnClickRemoveFilter(TSharedRef<SLevelSnapshotsEditorFilter> RemovedFilterWidget) const;
	bool AddTutorialTextAndCreateFilterWidgetIfEmpty() const;
	void AddChild(UNegatableFilter* AddedFilter, TSharedRef<FLevelSnapshotsEditorFilters> InEditorFilterModel, bool bSkipAnd = false) const;
	
	/** The horizontal box which contains all the filters */
	TSharedPtr<SWrapBox> FilterBox;
	/* Wiget with which user can add filters */
	TSharedPtr<SCreateNewFilterWidget> AddFilterWidget;

	TWeakObjectPtr<UConjunctionFilter> ManagedAndCondition;
	FDelegateHandle AddDelegateHandle;
};
