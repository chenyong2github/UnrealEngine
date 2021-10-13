// Copyright Epic Games, Inc. All Rights Reserved.

#include "SnapshotConsoleVariables.h"

#include "Containers/UnrealString.h"

TAutoConsoleVariable<FString> SnapshotCVars::CVarBreakOnSnapshotActor(TEXT("LevelSnapshots.BreakOnSnapshotActor"), TEXT(""), TEXT("Hit a debug break point when an actor with the given name is snapshot"));
TAutoConsoleVariable<FString> SnapshotCVars::CVarBreakOnDiffMatchedActor(TEXT("LevelSnapshots.BreakOnDiffMatchedActor"), TEXT(""), TEXT("Hit a debug break point when a matched actor with the given name is discovered while diffing world"));

#if UE_BUILD_DEBUG
TAutoConsoleVariable<FString> SnapshotCVars::CVarBreakOnSerializedPropertyName(TEXT("LevelSnapshots.BreakOnSerializedPropertyName"), TEXT(""), TEXT("Halt execution when encountering this property name during serialisation"));
#endif

TAutoConsoleVariable<bool> SnapshotCVars::CVarLogTimeDiffingMatchedActors(TEXT("LevelSnapshots.LogTimeDiffingMatchedActors"), false, TEXT("Enable logging of how long matched actors take to diff"));
TAutoConsoleVariable<bool> SnapshotCVars::CVarLogTimeTakingSnapshots(TEXT("LevelSnapshots.LogTimeTakingSnapshots"), false, TEXT("Enable logging of how long actors take to get snapshots taken off."));
TAutoConsoleVariable<bool> SnapshotCVars::CVarLogSelectionMap(TEXT("LevelSnapshots.LogSelectionMap"), false, TEXT("Logs all filtered properties a filter is applied."));
