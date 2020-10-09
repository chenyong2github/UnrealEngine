// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Results/SLevelSnapshotsEditorResults.h"

#include "Views/Results/LevelSnapshotsEditorResults.h"

#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

SLevelSnapshotsEditorResults::~SLevelSnapshotsEditorResults()
{
}

void SLevelSnapshotsEditorResults::Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorResults>& InEditorResults)
{
	EditorResultsPtr = InEditorResults;

	ChildSlot
		[
			SNew(STextBlock).Text(LOCTEXT("Results", "Results"))
		];
}

#undef LOCTEXT_NAMESPACE
