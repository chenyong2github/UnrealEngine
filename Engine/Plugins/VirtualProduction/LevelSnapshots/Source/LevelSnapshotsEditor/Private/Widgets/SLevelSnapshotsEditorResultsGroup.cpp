// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SLevelSnapshotsEditorResultsGroup.h"

SLevelSnapshotsEditorResultsGroup::~SLevelSnapshotsEditorResultsGroup()
{
}

void SLevelSnapshotsEditorResultsGroup::Construct(const FArguments& InArgs, const FString& InObjectPath, const FLevelSnapshot_Actor& InActorSnapshot)
{
	ObjectPath = InObjectPath;
	ActorSnapshot = InActorSnapshot;

	ChildSlot
	[
		SNew(STextBlock).Text(FText::FromString(ObjectPath))
	];
}
