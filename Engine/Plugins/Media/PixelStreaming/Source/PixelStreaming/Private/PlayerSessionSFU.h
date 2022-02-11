// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "PlayerSession.h"

namespace UE::PixelStreaming
{
	class FPlayerSessionDataOnly;

	class FPlayerSessionSFU : public FPlayerSession
	{
	public:
		FPlayerSessionSFU(FPlayerSessions* InSessions, FSignallingServerConnection* InSignallingServerConnection, FPixelStreamingPlayerId PlayerId);
		virtual ~FPlayerSessionSFU();

		void AddChildSession(TSharedPtr<FPlayerSessionDataOnly> ChildSession);

	private:
		TArray<TWeakPtr<FPlayerSessionDataOnly>> ChildSessions;
	};
} // namespace UE::PixelStreaming
