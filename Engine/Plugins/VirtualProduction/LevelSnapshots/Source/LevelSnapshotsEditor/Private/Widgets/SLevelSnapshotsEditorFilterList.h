// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FLevelSnapshotsEditorFilters;
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

	void AddChild(UNegatableFilter* AddedFilter, TSharedRef<FLevelSnapshotsEditorFilters> InEditorFilterModel) const;
	
	/** The horizontal box which contains all the filters */
	TSharedPtr<SWrapBox> FilterBox;

	TWeakObjectPtr<UConjunctionFilter> ManagedAndCondition;
	FDelegateHandle AddDelegateHandle;
};
