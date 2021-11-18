// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterAppExit.h"
#include "Misc/DisplayClusterLog.h"
#include "Engine/GameEngine.h"

#include "Async/Async.h"

#if WITH_EDITOR
#include "Editor/UnrealEd/Public/UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#endif

FCriticalSection FDisplayClusterAppExit::InternalsSyncScope;


void FDisplayClusterAppExit::ExitApplication(const FString& Msg)
{
	FScopeLock lock(&InternalsSyncScope);

	UE_LOG(LogDisplayClusterModule, Log, TEXT("Exit requested - %s"), *Msg);

	if (GEngine && GEngine->IsEditor())
	{
#if WITH_EDITOR
		GUnrealEd->RequestEndPlayMap();
#endif
	}
	else
	{
		if (!IsEngineExitRequested())
		{
			if (IsInGameThread())
			{
				FPlatformMisc::RequestExit(false);
			}
			else
			{
				AsyncTask(ENamedThreads::GameThread, []()
				{
					FPlatformMisc::RequestExit(false);
				});
			}
		}
	}
}
