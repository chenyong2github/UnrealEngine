// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerSessionSFU.h"
#include "PlayerSessions.h"
#include "PlayerSessionDataOnly.h"

namespace UE::PixelStreaming
{
	FPlayerSessionSFU::FPlayerSessionSFU(FPlayerSessions* InSessions, FSignallingServerConnection* InSignallingServerConnection, FPixelStreamingPlayerId InPlayerId)
		: FPlayerSession(InSessions, InSignallingServerConnection, InPlayerId)
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
