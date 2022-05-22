// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "PlayerSession.h"

namespace UE::PixelStreaming
{
	class FPlayerSessionDataOnly;

	class FPlayerSessionSFU : public FPlayerSession
	{
	public:
		FPlayerSessionSFU(FPlayerSessions* InSessions, FPixelStreamingSignallingConnection* InSignallingServerConnection, FPixelStreamingPlayerId PlayerId, TSharedPtr<IPixelStreamingInputDevice> InInputDevice);
		virtual ~FPlayerSessionSFU();

		void AddChildSession(TSharedPtr<FPlayerSessionDataOnly> ChildSession);
		virtual FName GetSessionType() const override { return Type; }

	public:
		inline static const FName Type = FName(TEXT("SFU"));

	private:
		TArray<TWeakPtr<FPlayerSessionDataOnly>> ChildSessions;
	};
} // namespace UE::PixelStreaming
