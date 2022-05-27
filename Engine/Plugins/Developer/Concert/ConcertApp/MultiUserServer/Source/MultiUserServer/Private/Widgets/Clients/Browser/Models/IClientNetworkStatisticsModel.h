// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FConcertSessionClientInfo;
struct FConcertSessionInfo;
struct FMessageAddress;
struct FMessageTransportStatistics;
struct FTransferStatistics;

namespace UE::MultiUserServer
{
	DECLARE_DELEGATE_OneParam(FOnMessageTransportStatisticsUpdated, const FTransferStatistics&);
	
	/** Decouples the UI from the server functions. */
	class IClientNetworkStatisticsModel
	{
	public:
		
		virtual TOptional<FMessageTransportStatistics> GetLatestNetworkStatistics(const FMessageAddress& ClientAddress) const = 0;

		virtual void RegisterOnTransferUpdatedFromThread(const FMessageAddress& ClientAddress, FOnMessageTransportStatisticsUpdated StatisticsUpdatedCallback) = 0;
		virtual void UnregisterOnTransferUpdatedFromThread(const FMessageAddress& ClientAddress) = 0;

		virtual ~IClientNetworkStatisticsModel() = default;
	};
}


