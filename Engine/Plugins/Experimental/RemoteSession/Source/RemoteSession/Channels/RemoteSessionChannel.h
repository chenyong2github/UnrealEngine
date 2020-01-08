// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RemoteSession/RemoteSessionRole.h"

class FBackChannelOSCConnection;

class REMOTESESSION_API IRemoteSessionChannel
{

public:

	IRemoteSessionChannel(ERemoteSessionChannelMode InRole, TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> InConnection) {}

	virtual ~IRemoteSessionChannel() {}

	virtual void Tick(const float InDeltaTime) = 0;

	virtual const TCHAR* GetType() const = 0;
};

class REMOTESESSION_API IRemoteSessionChannelFactoryWorker
{

public:

	virtual ~IRemoteSessionChannelFactoryWorker() {}

	virtual const TCHAR* GetType() const = 0;

	virtual TSharedPtr<IRemoteSessionChannel> Construct(ERemoteSessionChannelMode InMode, TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> InConnection) const = 0;
};
