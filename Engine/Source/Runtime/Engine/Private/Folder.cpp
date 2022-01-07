// Copyright Epic Games, Inc. All Rights Reserved.

#include "Folder.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "LevelUtils.h"

#if WITH_EDITOR
TOptional<FFolder::FRootObject> FFolder::GetOptionalFolderRootObject(const ULevel* InLevel)
{
	if (ULevelStreaming* LevelStreaming = InLevel ? ULevelStreaming::FindStreamingLevel(InLevel) : nullptr)
	{
		return LevelStreaming->GetFolderRootObject();
	}
	return FFolder::GetDefaultRootObject();
}
#endif