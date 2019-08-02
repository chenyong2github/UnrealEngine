// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

class FTimeSynthSoundWaveAssetActionExtender
{
public:
	static void RegisterMenus();
	static void ExecuteCreateTimeSyncClip(const struct FToolMenuContext& MenuContext);
	static void ExecuteCreateTimeSyncClipSet(const struct FToolMenuContext& MenuContext);
};

