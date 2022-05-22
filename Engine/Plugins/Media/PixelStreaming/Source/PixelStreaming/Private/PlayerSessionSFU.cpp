// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerSessionSFU.h"
#include "PlayerSessions.h"
#include "PlayerSessionDataOnly.h"

namespace UE::PixelStreaming
{
	FPlayerSessionSFU::FPlayerSessionSFU(FPlayerSessions* InSessions, FPixelStreamingSignallingConnection* InSignallingServerConnection, FPixelStreamingPlayerId InPlayerId, TSharedPtr<IPixelStreamingInputDevice> InInputDevice)
		: FPlayerSession(InSessions, InSignallingServerConnection, InPlayerId, InInputDevice)
	{
	}

	FPlayerSessionSFU::~FPlayerSessionSFU()
	{
		// remove any children
		for (auto&& WeakChild : ChildSessions)
		{
			if (auto Child = WeakChild.Pin())
			{
				PlayerSessions->DeletePlayerSession(Child->GetPlayerId());
			}
		}
	}

	void FPlayerSessionSFU::AddChildSession(TSharedPtr<FPlayerSessionDataOnly> ChildSession)
	{
		ChildSessions.Add(ChildSession);
	}
} // namespace UE::PixelStreaming
