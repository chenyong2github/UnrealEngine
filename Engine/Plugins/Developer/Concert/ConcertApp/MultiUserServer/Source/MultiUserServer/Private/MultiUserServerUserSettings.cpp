// Copyright Epic Games, Inc. All Rights Reserved.

#include "MultiUserServerUserSettings.h"

#include "Misc/CoreDelegates.h"

namespace UE::MultiUserServer
{
	static bool bIsShutdown = false;
}

UMultiUserServerUserSettings::UMultiUserServerUserSettings()
{
	OnSessionBrowserColumnVisibilityChanged().AddLambda([this](const FColumnVisibilitySnapshot&)
	{
		SaveConfig();
	});
	OnArchivedActivityBrowserColumnVisibility().AddLambda([this](const FColumnVisibilitySnapshot&)
	{
		SaveConfig();
	});
	OnLiveActivityBrowserColumnVisibility().AddLambda([this](const FColumnVisibilitySnapshot&)
	{
		SaveConfig();
	});
	OnLiveSessionContentColumnVisibility().AddLambda([this](const FColumnVisibilitySnapshot&)
	{
		SaveConfig();
	});

	FCoreDelegates::OnPreExit.AddLambda([]()
	{
		UE::MultiUserServer::bIsShutdown = true;
	});
}

UMultiUserServerUserSettings* UMultiUserServerUserSettings::GetUserSettings()
{
	// If we're shutting down GetMutableDefault will return garbage - this function may be called by destructor when this module is unloaded
	return UE::MultiUserServer::bIsShutdown
		? nullptr
		: GetMutableDefault<UMultiUserServerUserSettings>();
}
