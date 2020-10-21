// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SLevelSnapshotsEditorFilterList.h"

#include "Widgets/SLevelSnapshotsEditorFilter.h"
#include "Views/Filter/LevelSnapshotsEditorFilters.h"

#include "Widgets/Layout/SWrapBox.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

SLevelSnapshotsEditorFilterList::~SLevelSnapshotsEditorFilterList()
{
}

void SLevelSnapshotsEditorFilterList::Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorFilters>& InFilters)
{
	FiltersModelPtr = InFilters;

	FilterBox = SNew(SWrapBox)
		.UseAllottedSize(true);

	ChildSlot
	[
		FilterBox.ToSharedRef()
	];
}

void SLevelSnapshotsEditorFilterList::AddFilter(const FName& InName,  ULevelSnapshotFilter* InFilter)
{
	FilterBox->AddSlot()
		.Padding(3, 3)
		[
			SNew(SLevelSnapshotsEditorFilter, InFilter, FiltersModelPtr.Pin().ToSharedRef())
				.Text(FText::FromName(InName))
				.FilterColor(FLinearColor::Green)
		];
}

#undef LOCTEXT_NAMESPACE