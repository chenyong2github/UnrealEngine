// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapMusicServicePlugin.h"

FMagicLeapMusicServiceCallbacks& FMagicLeapMusicServicePlugin::GetDelegates()
{
	return Delegates;
}


IMPLEMENT_MODULE(FMagicLeapMusicServicePlugin, MagicLeapMusicService);

