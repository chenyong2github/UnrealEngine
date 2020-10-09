// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SLevelSnapshotsEditorFilterList.h"

#include "Widgets/SLevelSnapshotsEditorFilter.h"

#include "Widgets/Layout/SWrapBox.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

SLevelSnapshotsEditorFilterList::~SLevelSnapshotsEditorFilterList()
{
}

void SLevelSnapshotsEditorFilterList::Construct(const FArguments& InArgs)
{
	FilterBox = SNew(SWrapBox)
		.UseAllottedSize(true);

	// TODO. Add test Filters
	FilterBox->AddSlot()
		.Padding(3, 3)
		[
			SNew(SLevelSnapshotsEditorFilter).Text(LOCTEXT("Location", "Location")).FilterColor(FLinearColor::Blue)
		];

	FilterBox->AddSlot()
		.Padding(3, 3)
		[
			SNew(SLevelSnapshotsEditorFilter).Text(LOCTEXT("Rotation", "Rotation")).FilterColor(FLinearColor::Red)
		];

	FilterBox->AddSlot()
		.Padding(3, 3)
		[
			SNew(SLevelSnapshotsEditorFilter).Text(LOCTEXT("Scale", "Scale")).FilterColor(FLinearColor::Green)
		];

	ChildSlot
	[
		FilterBox.ToSharedRef()
	];
}

#undef LOCTEXT_NAMESPACE