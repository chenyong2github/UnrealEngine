// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ConjunctionFilter.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"


class SLevelSnapshotsEditorFilters;
class SLevelSnapshotsEditorFilterList;

/* Creates all widgets needed to show an AND-condition of filters. */
class SLevelSnapshotsEditorFilterRow : public SCompoundWidget
{
public:

	DECLARE_DELEGATE_OneParam(FOnClickRemoveRow, TSharedRef<SLevelSnapshotsEditorFilterRow>);
	
	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorFilterRow)
	{}
		SLATE_EVENT(FOnClickRemoveRow, OnClickRemoveRow)
	SLATE_END_ARGS()

	void Construct(
		const FArguments& InArgs, 
		const TSharedRef<SLevelSnapshotsEditorFilters>& InEditorFilters, 
		UConjunctionFilter* InManagedFilter
	);

	const TWeakObjectPtr<UConjunctionFilter>& GetManagedFilter();
	
private:

	FOnClickRemoveRow OnClickRemoveRow;

	TWeakObjectPtr<UConjunctionFilter> ManagedFilterWeakPtr;
	/* Stores all filters */
	TSharedPtr<SLevelSnapshotsEditorFilterList> FilterList;

};

