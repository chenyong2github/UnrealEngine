// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#if PLATFORM_HAS_BSD_SOCKETS

#include "EventLoop/BSDSocket/BSDSocketTypes.h"
#include "EventLoop/BSDSocket/EventLoopIOAccessBSDSocket.h"
#include "EventLoop/IEventLoopIOManager.h"
#include "EventLoop/EventLoopManagedStorage.h"
#include "Templates/SharedPointer.h"

namespace UE::EventLoop {

class EVENTLOOP_API FIOManagerBSDSocketSelect final : public IIOManager
{
public:
	using FIOAccess = FIOAccessBSDSocket;

	struct FParams
	{
	};

	struct FConfig
	{
	};

	FIOManagerBSDSocketSelect(IEventLoop& EventLoop, FParams&& Params);
	virtual ~FIOManagerBSDSocketSelect() = default;
	virtual bool Init() override;
	virtual void Shutdown() override;
	virtual void Notify() override;
	virtual void Poll(FTimespan WaitTime) override;

	FIOAccess& GetIOAccess();

private:
	TSharedRef<class FIOManagerBSDSocketSelectImpl> Impl;
};

/* UE::EventLoop */ }

#endif // PLATFORM_HAS_BSD_SOCKETS
