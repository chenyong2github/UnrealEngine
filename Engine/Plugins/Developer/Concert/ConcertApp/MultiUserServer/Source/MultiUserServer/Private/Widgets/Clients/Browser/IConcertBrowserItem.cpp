// Copyright Epic Games, Inc. All Rights Reserved.

#include "IConcertBrowserItem.h"

#include "Models/IClientNetworkStatisticsModel.h"

#include "IMessageContext.h"
#include "INetworkMessagingExtension.h"

namespace UE::MultiUserServer
{
	FConcertBrowserItemCommonImpl::FConcertBrowserItemCommonImpl(TSharedRef<IClientNetworkStatisticsModel> NetworkStatisticsModel)
			: NetworkStatisticsModel(MoveTemp(NetworkStatisticsModel))
	{}

	void FConcertBrowserItemCommonImpl::SetDisplayMode(EConcertBrowserItemDisplayMode Value)
	{
		ItemDisplayMode = Value;
	}
	
	EConcertBrowserItemDisplayMode FConcertBrowserItemCommonImpl::GetDisplayMode() const
	{
		return ItemDisplayMode;
	}

	void FConcertBrowserItemCommonImpl::AppendSearchTerms(TArray<FString>& SearchTerms) const
	{
		SearchTerms.Add(GetDisplayName());
		const TOptional<FMessageTransportStatistics> Stats = GetLatestNetworkStatistics();
		SearchTerms.Add(NetworkStatistics::FormatIPv4AsString(Stats));
		
		if (Stats)
		{
			SearchTerms.Add(NetworkStatistics::FormatTotalBytesSent(*Stats));
			SearchTerms.Add(NetworkStatistics::FormatTotalBytesReceived(*Stats));
			SearchTerms.Add(NetworkStatistics::FormatAverageRTT(*Stats));
			SearchTerms.Add(NetworkStatistics::FormatBytesInflight(*Stats));
			SearchTerms.Add(NetworkStatistics::FormatTotalBytesLost(*Stats));
		}
	}

	TOptional<FMessageTransportStatistics> FConcertBrowserItemCommonImpl::GetLatestNetworkStatistics() const
	{
		return NetworkStatisticsModel->GetLatestNetworkStatistics(GetMessageAddress());
	}
	
	bool FConcertBrowserItemCommonImpl::IsOnline() const
	{
		return NetworkStatisticsModel->IsOnline(GetMessageAddress());
	}
}
