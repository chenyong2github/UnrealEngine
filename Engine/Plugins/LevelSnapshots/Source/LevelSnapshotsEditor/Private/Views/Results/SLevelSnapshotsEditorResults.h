// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FLevelSnapshotsEditorResults;

class SLevelSnapshotsEditorResults : public SCompoundWidget
{
public:
	~SLevelSnapshotsEditorResults();

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorResults)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorResults>& InEditorResults);

private:
	TWeakPtr<FLevelSnapshotsEditorResults> EditorResultsPtr;
};
