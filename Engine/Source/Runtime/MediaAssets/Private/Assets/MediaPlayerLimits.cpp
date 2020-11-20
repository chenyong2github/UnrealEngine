// Copyright Epic Games, Inc. All Rights Reserved.
#include "MediaPlayerLimits.h"
#include "Misc/ConfigCacheIni.h"

int32 UMediaPlayerLimits::CurrentPlayerCount = 0;
int32 UMediaPlayerLimits::MaxPlayerCount = -1;
FCriticalSection UMediaPlayerLimits::AccessLock;

UMediaPlayerLimits::UMediaPlayerLimits() : Super()
{
	GConfig->GetInt(TEXT("/Script/MediaAssets/MediaPlayer"), TEXT("MaxNumberOfMediaPlayers"), MaxPlayerCount, GEngineIni);
}

bool UMediaPlayerLimits::ClaimPlayer()
{
	FScopeLock lock(&AccessLock);
	if (MaxPlayerCount == -1)
	{
		return true;
	}
	else if (CurrentPlayerCount < MaxPlayerCount)
	{
		CurrentPlayerCount++;
		return true;
	}
	return false;
}

void UMediaPlayerLimits::ReleasePlayer()
{
	FScopeLock lock(&AccessLock);
	if (CurrentPlayerCount > 0)
	{
		CurrentPlayerCount--;
	}
}