// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class ULevelSnapshotsEditorData;

struct FSnapshotEditorViewData
{
	TWeakObjectPtr<ULevelSnapshotsEditorData> EditorDataPtr = nullptr;
};
