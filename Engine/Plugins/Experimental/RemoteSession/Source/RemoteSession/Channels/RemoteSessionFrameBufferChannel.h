// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RemoteSessionImageChannel.h"

// FRemoteSessionFrameBufferChannel is deprecated. Please use FRemoteSessionImageChannel.
// FRemoteSessionFrameBufferChannelFactoryWorker was created for backward compatibility with older app

class REMOTESESSION_API FRemoteSessionFrameBufferChannelFactoryWorker : public IRemoteSessionChannelFactoryWorker
{
public:
	static const TCHAR* StaticType() { return TEXT("FRemoteSessionFrameBufferChannel_DEPRECATED"); }
	virtual const TCHAR* GetType() const override { return StaticType(); }
	virtual TSharedPtr<IRemoteSessionChannel> Construct(ERemoteSessionChannelMode InMode, TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> InConnection) const override;
};

