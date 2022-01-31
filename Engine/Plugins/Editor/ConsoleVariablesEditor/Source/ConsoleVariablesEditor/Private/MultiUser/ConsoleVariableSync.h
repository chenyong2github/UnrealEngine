// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "Delegates/Delegate.h"
#include "Delegates/DelegateCombinations.h"
#include "Templates/UniquePtr.h"

#include "ConcertMessages.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRemoteCVarChange, FString, FString);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMultiUserConnectionChange, EConcertConnectionStatus);

namespace UE::ConsoleVariables::MultiUser::Private
{

struct FManagerImpl;

struct FManager
{
	FManager();
	~FManager();

	// Do not allow this to be copied or moved.
	FManager(const FManager&) = delete;
	FManager(FManager&&) = delete;
	FManager &operator=(const FManager&) = delete;
	FManager &operator=(FManager&&) = delete;

	/** Delegate that is invoked when a remote client has sent a new console variable value.*/
	FOnRemoteCVarChange& OnRemoteCVarChange();

	/** Delegate that is invoked when the connection status changes for this client. */
	FOnMultiUserConnectionChange& OnConnectionChange();

	/** Sends the named console variable with value to all connected Multi-user clients */
	void SendConsoleVariableChange(FString InName, FString InValue);

	/** Enables / disables multi-user message handling. */
	void SetEnableMultiUserSupport(bool bIsEnabled);

	bool IsInitialized() const;
	
private:
	TUniquePtr<FManagerImpl> Implementation;
};

}
