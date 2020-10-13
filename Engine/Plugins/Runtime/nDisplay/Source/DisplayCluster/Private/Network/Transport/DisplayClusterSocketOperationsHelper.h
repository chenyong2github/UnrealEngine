// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Network/Transport/DisplayClusterSocketOperations.h"
#include "Network/Packet/IDisplayClusterPacket.h"

#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterAppExit.h"


/**
 * Socket operations helper. High level operations with specific packet types.
 */
template <typename TPacketType, bool bExitOnCommError>
class FDisplayClusterSocketOperationsHelper
{
	static_assert(std::is_base_of<IDisplayClusterPacket, TPacketType>::value, "TPacketType is not derived from IDisplayClusterPacket");

public:
	FDisplayClusterSocketOperationsHelper(FDisplayClusterSocketOperations& InSocketOps, const FString& InLogHeader = FString())
		: SocketOps(InSocketOps)
		, LogHeader(InLogHeader)
	{ }

	virtual ~FDisplayClusterSocketOperationsHelper()
	{ }

public:
	TSharedPtr<TPacketType> SendRecvPacket(const TSharedPtr<TPacketType>& Request)
	{
		if (Request)
		{
			if (SendPacket(Request))
			{
				return ReceivePacket();
			}
		}

		return nullptr;
	}

	bool SendPacket(const TSharedPtr<TPacketType>& Packet)
	{
		if (!Packet && bExitOnCommError)
		{
			FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::EExitType::NormalSoft, FString("Net error. Wrong packet data (nullptr). Closing the application..."));
			return false;
		}

		UE_LOG(LogDisplayClusterNetworkMsg, VeryVerbose, TEXT("%s: sending packet - %s"),
			LogHeader.IsEmpty() ? *SocketOps.GetConnectionName() : *LogHeader,
			*StaticCastSharedPtr<IDisplayClusterPacket>(Packet)->ToLogString(true));

		const bool bResult = StaticCastSharedPtr<IDisplayClusterPacket>(Packet)->SendPacket(SocketOps);

		if (!bResult && bExitOnCommError)
		{
			FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::EExitType::NormalSoft, FString("Net error. Couldn't send a packet. Closing the application..."));
		}

		return bResult;
	}

	TSharedPtr<TPacketType> ReceivePacket()
	{
		TSharedPtr<TPacketType> Packet = MakeShared<TPacketType>();
		const bool bResult = StaticCastSharedPtr<IDisplayClusterPacket>(Packet)->RecvPacket(SocketOps);
		
		if (!bResult)
		{
			if (bExitOnCommError)
			{
				FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::EExitType::NormalSoft, FString("Net error. Couldn't receive a packet. Closing the application..."));
			}

			return nullptr;
		}

		UE_LOG(LogDisplayClusterNetworkMsg, VeryVerbose, TEXT("%s: received packet - %s"),
			LogHeader.IsEmpty() ? *SocketOps.GetConnectionName() : *LogHeader,
			*StaticCastSharedPtr<IDisplayClusterPacket>(Packet)->ToLogString(true));

		return Packet;
	}

private:
	FDisplayClusterSocketOperations& SocketOps;
	FString LogHeader;
};
