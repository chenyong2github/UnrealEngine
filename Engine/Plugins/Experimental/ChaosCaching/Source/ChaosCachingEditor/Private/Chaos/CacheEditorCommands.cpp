// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/CacheEditorCommands.h"

#define LOCTEXT_NAMESPACE "CacheEditorCommands"

void FCachingEditorCommands::RegisterCommands()
{
	UI_COMMAND(CreateCacheManager, "Create Cache Manager", "Adds a cache manager to observe compatible components in the selection set.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SetManagerRecordAll, "Set Record All", "Sets selected cache managers to record all of their observed components.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SetManagerPlayAll, "Set Play All", "Sets selected cache managers to play all of their observed components.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SetManagerTriggerAll, "Set Trigger All", "Sets selected cache managers to set all of their observed components to triggered.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SetManagerTimedAll, "Set Timed All", "Sets selected cache managers to set all of their observed components to timed.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
