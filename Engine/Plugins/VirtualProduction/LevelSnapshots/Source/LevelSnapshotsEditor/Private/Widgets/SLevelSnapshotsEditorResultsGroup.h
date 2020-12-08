// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "LevelSnapshot.h"

class SSearchBox;

class SLevelSnapshotsEditorResultsGroup : public SCompoundWidget
{
public:
	~SLevelSnapshotsEditorResultsGroup();

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorResultsGroup)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FString& InObjectPath, const FLevelSnapshot_Actor& InActorSnapshot);

private:
	FString ObjectPath;

	FLevelSnapshot_Actor ActorSnapshot;
};

