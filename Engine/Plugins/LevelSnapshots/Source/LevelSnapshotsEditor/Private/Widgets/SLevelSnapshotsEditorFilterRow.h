// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

struct FLevelSnapshotsEditorFilterRowGroup;
class SLevelSnapshotsEditorFilters;
class SLevelSnapshotsEditorFilterList;

class SLevelSnapshotsEditorFilterRow : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorFilterRow)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<SLevelSnapshotsEditorFilters>& InEditorFilters, const TSharedRef<FLevelSnapshotsEditorFilterRowGroup>& InFieldGroup);

private:

	FReply RemoveFilter();

	TWeakPtr<SLevelSnapshotsEditorFilters> EditorFiltersPtr;
	TWeakPtr<FLevelSnapshotsEditorFilterRowGroup> FieldGroupPtr;
	
	TSharedPtr<SLevelSnapshotsEditorFilterList> FilterList;

};

