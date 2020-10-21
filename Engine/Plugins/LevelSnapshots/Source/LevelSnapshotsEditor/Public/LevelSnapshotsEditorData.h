// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "LevelSnapshotsEditorData.generated.h"

class ULevelSnapshotFilter;

UCLASS()
class ULevelSnapshotEditorFilterGroup : public UObject
{
	GENERATED_BODY()

public:
	ULevelSnapshotFilter* AddOrFindFilter(TSubclassOf<ULevelSnapshotFilter> InClass, const FName& InName);

public:
	UPROPERTY()
	TMap<FName, ULevelSnapshotFilter*> Filters;
};

UCLASS()
class LEVELSNAPSHOTSEDITOR_API ULevelSnapshotsEditorData : public UObject
{
	GENERATED_BODY()

public:
	ULevelSnapshotEditorFilterGroup* AddOrFindGroup(const FName& InName);

public:
	UPROPERTY()
	TMap<FName, ULevelSnapshotEditorFilterGroup*> FilterGroups;
};