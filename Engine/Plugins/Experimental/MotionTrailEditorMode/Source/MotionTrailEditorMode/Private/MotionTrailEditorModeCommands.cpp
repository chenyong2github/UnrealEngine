// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotionTrailEditorModeCommands.h"
#include "MotionTrailEditorMode.h"


#define LOCTEXT_NAMESPACE "MotionTrailEditorModeCommands"

namespace UE
{
namespace MotionTrailEditor
{

void FMotionTrailEditorModeCommands::RegisterCommands()
{
	UI_COMMAND(Default, "Default", "Default trail editing tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	Commands.Add(UMotionTrailEditorMode::MotionTrailEditorMode_Default, { Default });
}

void FMotionTrailEditorModeCommands::RegisterDynamic(const FName InName, const TArray<TSharedPtr<FUICommandInfo>>& InCommands)
{
	Instance.Pin()->Commands.Add(InName, InCommands);
	Instance.Pin()->CommandsChanged.Broadcast(*(Instance.Pin()));
}

void FMotionTrailEditorModeCommands::UnRegisterDynamic(const FName InName)
{
	Instance.Pin()->Commands.Remove(InName);
	Instance.Pin()->CommandsChanged.Broadcast(*(Instance.Pin()));
}

} // namespace MovieScene
} // namespace UE

#undef LOCTEXT_NAMESPACE
