// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

enum class ERemoteCacheState : uint8
{
	Idle,
	Busy,
	Unavailable,
	Warning,
};

class FDerivedDataInformation
{
public:

	static double				GetCacheActivityTimeSeconds(bool bGet, bool bLocal);
	static double				GetCacheActivitySizeBytes(bool bGet, bool bLocal);
	static bool					GetHasRemoteCache();
	static bool					GetHasZenCache();
	static bool					GetHasHordeStorageCache();
	static ERemoteCacheState	GetRemoteCacheState() { return RemoteCacheState; }
	static FText				GetRemoteCacheStateAsText();
	static FText				GetRemoteCacheWarningMessage() { return RemoteCacheWarningMessage; }
	static void					UpdateRemoteCacheState();
	static bool					IsUploading() { return bIsUploading; }
	static bool					IsDownloading() { return bIsDownloading; }

private:

	static ERemoteCacheState	RemoteCacheState;
	static FText				RemoteCacheWarningMessage;
	static double				LastGetTime;
	static double				LastPutTime;
	static bool					bIsUploading;
	static bool					bIsDownloading;

};
