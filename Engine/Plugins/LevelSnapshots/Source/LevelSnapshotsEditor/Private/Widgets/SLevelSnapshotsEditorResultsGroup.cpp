// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SLevelSnapshotsEditorResultsGroup.h"

SLevelSnapshotsEditorResultsGroup::~SLevelSnapshotsEditorResultsGroup()
{
}

void SLevelSnapshotsEditorResultsGroup::Construct(const FArguments& InArgs, const FString& InObjectPath, const FActorSnapshot& InActorSnapshot)
{
	ObjectPath = InObjectPath;
	ActorSnapshot = InActorSnapshot;

	ChildSlot
		[
			SNew(STextBlock).Text(FText::FromString(ObjectPath))
		];
}
