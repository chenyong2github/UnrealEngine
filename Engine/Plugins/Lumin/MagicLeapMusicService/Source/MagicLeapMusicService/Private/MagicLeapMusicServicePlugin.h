// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMagicLeapMusicServicePlugin.h"
#include "Lumin/CAPIShims/LuminAPI.h"
#include "MagicLeapPluginUtil.h"

class FMagicLeapMusicServicePlugin : public IMagicLeapMusicServicePlugin
{
public:

	// Returns the callbacks associated with the music service plugin. 
	FMagicLeapMusicServiceCallbacks& GetDelegates() override;

private:

	FMagicLeapMusicServiceCallbacks Delegates;

};

inline FMagicLeapMusicServicePlugin& GetMagicLeapMusicServiceModule()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapMusicServicePlugin>("MagicLeapMusicService");
}
