// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenericPlatform/ScreenReaderBase.h"
#include "GenericPlatform/ScreenReaderApplicationMessageHandlerBase.h"
#include "GenericPlatform/GenericApplication.h"
#include "GenericPlatform/ScreenReaderUser.h"
#include "Announcement/ScreenReaderAnnouncementChannel.h"
#include "ScreenReaderLog.h"


FScreenReaderBase::FScreenReaderBase(const TSharedRef<GenericApplication>& InPlatformApplication)
	: ScreenReaderApplicationMessageHandler(MakeShared<FScreenReaderApplicationMessageHandlerBase>(InPlatformApplication->GetMessageHandler(), *this))
	, PlatformApplication(InPlatformApplication)
	, bActive(false)
{

}

FScreenReaderBase::~FScreenReaderBase()
{

}

void FScreenReaderBase::Activate()
{
	if (!IsActive())
	{
		checkf(PlatformApplication.IsValid(), TEXT("Trying to activate the screen reader with an invalid platform aplication.with "));
		UE_LOG(LogScreenReader, Verbose, TEXT("Activating screen reader."));
		// Set the screen reader application message handler to be the platform's message handler 
		// This allows the screen reader to intercept all applications before passing them on to Slate 
		PlatformApplication.Pin()->SetMessageHandler(ScreenReaderApplicationMessageHandler);
		OnActivate();
		bActive = true;
}
}

void FScreenReaderBase::Deactivate()
{
	if (IsActive())
	{
		UE_LOG(LogScreenReader, Verbose, TEXT("Deactivating screen reader."));
		OnDeactivate();
		// Set FSlateApplication as the platform message ahndler again 
		PlatformApplication.Pin()->SetMessageHandler(ScreenReaderApplicationMessageHandler->GetTargetMessageHandler());
		bActive = false;
	}
}

bool FScreenReaderBase::RegisterUser(int32 InUserId)
{
	if (UsersMap.Contains(InUserId))
	{
		UE_LOG(LogScreenReader, Verbose, TEXT("Failed to register screen reader user with Id %d. Another user with the same Id has already been registered."), InUserId);
		return false;
	}
	UsersMap.Add(InUserId, MakeShared<FScreenReaderUser>(InUserId));
	UE_LOG(LogScreenReader, Verbose, TEXT("Registered screen reader user %d."), InUserId);
	return true;
}

bool FScreenReaderBase::UnregisterUser(int32 InUserId)
{
	if (!UsersMap.Contains(InUserId))
	{
		UE_LOG(LogScreenReader, Verbose, TEXT("Failed to unregister with Id %d. No user with %d Id is registered."), InUserId, InUserId);
		return false;
	}
	UsersMap[InUserId]->Deactivate();
	UsersMap.Remove(InUserId);
	UE_LOG(LogScreenReader, Verbose, TEXT("Unregistered screen reader user %d."), InUserId);
	return true;
}

bool FScreenReaderBase::IsUserRegistered(int32 InUserId) const
{
	return UsersMap.Contains(InUserId);
}

void FScreenReaderBase::UnregisterAllUsers()
{
	for (TPair<int32, TSharedRef<FScreenReaderUser>>& UserPair : UsersMap)
	{
		UserPair.Value->Deactivate();
	}
	UsersMap.Empty();
}

TSharedRef<FScreenReaderUser> FScreenReaderBase::GetUserChecked(int32 InUserId) const
{
	checkf(IsUserRegistered(InUserId), TEXT("User Id %d is not registered. Did you forget to register the user with RegisterUser()?"), InUserId);
	return UsersMap[InUserId];
}
	
TSharedPtr<FScreenReaderUser> FScreenReaderBase::GetUser(int32 InUserId) const
{
	if (IsUserRegistered(InUserId))
	{
		return UsersMap[InUserId];
	}
	return nullptr;
}