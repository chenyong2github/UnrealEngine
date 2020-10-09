// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SWrapBox;

class SLevelSnapshotsEditorFilterList : public SCompoundWidget
{
public:
	~SLevelSnapshotsEditorFilterList();

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorFilterList)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	/** The horizontal box which contains all the filters */
	TSharedPtr<SWrapBox> FilterBox;
};
