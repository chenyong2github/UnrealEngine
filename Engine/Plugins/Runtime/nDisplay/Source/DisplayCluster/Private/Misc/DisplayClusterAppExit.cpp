// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterAppExit.h"
#include "Misc/DisplayClusterLog.h"
#include "Engine/GameEngine.h"

#if WITH_EDITOR
#include "Editor/UnrealEd/Public/UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#endif

FCriticalSection FDisplayClusterAppExit::InternalsSyncScope;

auto FDisplayClusterAppExit::ExitTypeToStr(EExitType ExitType)
{
	switch (ExitType)
	{
	case EExitType::KillImmediately:
		return TEXT("KILL");
	case EExitType::NormalSoft:
		return TEXT("UE_soft");
	case EExitType::NormalForce:
		return TEXT("UE_force");
	default:
		return TEXT("unknown");
	}
}

void FDisplayClusterAppExit::ExitApplication(EExitType ExitType, const FString& Msg)
{
	if (GEngine && GEngine->IsEditor())
	{
#if WITH_EDITOR
		UE_LOG(LogDisplayClusterModule, Log, TEXT("PIE STOP: %s application quit requested: %s"), ExitTypeToStr(ExitType), *Msg);
		GUnrealEd->RequestEndPlayMap();
#endif
		return;
	}
	else
	{
		FScopeLock lock(&InternalsSyncScope);

		// We process only first call. Thus we won't have a lot of requests from different socket threads.
		// We also will know the first requester which may be useful in step-by-step problem solving.
		static bool bRequestedBefore = false;
		if (bRequestedBefore == false || ExitType == EExitType::KillImmediately)
		{
			bRequestedBefore = true;
			UE_LOG(LogDisplayClusterModule, Log, TEXT("%s application quit requested: %s"), ExitTypeToStr(ExitType), *Msg);

			GLog->Flush();

			switch (ExitType)
			{
				case EExitType::KillImmediately:
				{
					FProcHandle hProc = FPlatformProcess::OpenProcess(FPlatformProcess::GetCurrentProcessId());
					FPlatformProcess::TerminateProc(hProc, true);
					break;
				}

				case EExitType::NormalSoft:
				{
					FProcHandle hProc = FPlatformProcess::OpenProcess(FPlatformProcess::GetCurrentProcessId());
					FPlatformProcess::TerminateProc(hProc, true);
					break;
				}

				case EExitType::NormalForce:
				{
					FPlatformMisc::RequestExit(true);
					break;
				}

				default:
				{
					UE_LOG(LogDisplayClusterModule, Warning, TEXT("Unknown exit type requested"));
					break;
				}
			}
		}
	}
}
