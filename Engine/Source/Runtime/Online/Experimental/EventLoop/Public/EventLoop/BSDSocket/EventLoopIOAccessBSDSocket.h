// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#if PLATFORM_HAS_BSD_SOCKETS

#include "EventLoop/BSDSocket/BSDSocketTypes.h"
#include "EventLoop/IEventLoopIOManager.h"
#include "EventLoop/EventLoopManagedStorage.h"
#include "Templates/Function.h"

namespace UE::EventLoop {

using FIOCallback = TUniqueFunction<void(EIOFlags SignaledFlags)>;

struct FIORequestBSDSocket
{
	SOCKET Socket = INVALID_SOCKET;
	EIOFlags Flags = EIOFlags::None;
	FIOCallback Callback;
};

class FIOAccessBSDSocket final : public FNoncopyable
{
public:
	struct FStorageTraits : public FManagedStorageDefaultTraits
	{
		using FExternalHandle = FIORequestHandle;
	};

	using FStorageType = TManagedStorage<FIORequestBSDSocket, FStorageTraits>;

	FIOAccessBSDSocket(FStorageType& InIORequestStorage)
		: IORequestStorage(InIORequestStorage)
	{
	}

	FIORequestHandle CreateSocketIORequest(FIORequestBSDSocket&& Request)
	{
		if (Request.Socket == INVALID_SOCKET || Request.Flags == EIOFlags::None || !Request.Callback)
		{
			return FIORequestHandle();
		}

		return IORequestStorage.Add({MoveTemp(Request)});
	}

	void DestroyIORequest(FIORequestHandle& Handle)
	{
		IORequestStorage.Remove(Handle);
	}

private:
	FStorageType& IORequestStorage;
};

/* UE::EventLoop */ }

#endif // PLATFORM_HAS_BSD_SOCKETS
