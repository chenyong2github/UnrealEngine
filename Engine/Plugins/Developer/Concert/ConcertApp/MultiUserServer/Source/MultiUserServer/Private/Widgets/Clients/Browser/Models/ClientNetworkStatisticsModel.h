// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IClientNetworkStatisticsModel.h"

namespace UE::MultiUserServer
{
	/** Synchronizes the network statistics (the statistics are updated async). */
	class FClientNetworkStatisticsModel : public IClientNetworkStatisticsModel
	{
	public:

		FClientNetworkStatisticsModel();
		virtual ~FClientNetworkStatisticsModel() override;

		//~ Begin IClientNetworkStatisticsModel Interface
		virtual TOptional<FMessageTransportStatistics> GetLatestNetworkStatistics(const FMessageAddress& ClientAddress) const override;
		virtual void RegisterOnTransferUpdatedFromThread(const FMessageAddress& ClientAddress, FOnMessageTransportStatisticsUpdated StatisticsUpdatedCallback) override;
		virtual void UnregisterOnTransferUpdatedFromThread(const FMessageAddress& ClientAddress) override;
		//~ End IClientNetworkStatisticsModel Interface

	private:

		TMap<FGuid, FOnMessageTransportStatisticsUpdated> StatisticUpdateCallbacks;

		void OnTransferUpdatedFromThread(FTransferStatistics Stats) const;
	};
}

