// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemEOSPlus.h"

#include "Misc/NetworkVersion.h"
#include "Misc/App.h"


bool FOnlineSubsystemEOSPlus::Tick(float DeltaTime)
{
	return FOnlineSubsystemImpl::Tick(DeltaTime);
}

bool FOnlineSubsystemEOSPlus::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	return false;
}

FString FOnlineSubsystemEOSPlus::GetAppId() const
{
	return TEXT("");
}

FText FOnlineSubsystemEOSPlus::GetOnlineServiceName() const
{
	return NSLOCTEXT("OnlineSubsystemEOSPlus", "OnlineServiceName", "EOS_Plus");
}
