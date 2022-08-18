// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClientTransferStatisticsModel.h"

#include "INetworkMessagingExtension.h"
#include "Containers/Ticker.h"
#include "Features/IModularFeatures.h"

namespace UE::MultiUserServer
{
	namespace Private::ClientTransferStatisticsModel
	{
		static INetworkMessagingExtension* GetMessagingStatistics()
		{
			IModularFeatures& ModularFeatures = IModularFeatures::Get();
			if (IsInGameThread())
			{
				if (ModularFeatures.IsModularFeatureAvailable(INetworkMessagingExtension::ModularFeatureName))
				{
					return &ModularFeatures.GetModularFeature<INetworkMessagingExtension>(INetworkMessagingExtension::ModularFeatureName);
				}
			}
			else
			{
				ModularFeatures.LockModularFeatureList();
				ON_SCOPE_EXIT
				{
					ModularFeatures.UnlockModularFeatureList();
				};
			
				if (ModularFeatures.IsModularFeatureAvailable(INetworkMessagingExtension::ModularFeatureName))
				{
					return &ModularFeatures.GetModularFeature<INetworkMessagingExtension>(INetworkMessagingExtension::ModularFeatureName);
				}
			}
		
			ensureMsgf(false, TEXT("Feature %s is unavailable"), *INetworkMessagingExtension::ModularFeatureName.ToString());
			return nullptr;
		}
	}

	FClientTransferStatisticsModel::FClientTransferStatisticsModel(const FMessageAddress& ClientAddress)
		: ClientAddress(ClientAddress)
		, OutboundStatTracker([this](const FOutboundTransferStatistics& Stats) { return IsSentToClient(Stats); }, [](const FOutboundTransferStatistics& Stats){ return Stats.BytesSent; })
		, InboundStatTracker([this](const FInboundTransferStatistics& Stats) { return IsReceivedFromClient(Stats); }, [](const FInboundTransferStatistics& Stats){ return Stats.BytesReceived; })
	{
		if (INetworkMessagingExtension* Statistics = Private::ClientTransferStatisticsModel::GetMessagingStatistics())
		{
			Statistics->OnOutboundTransferUpdatedFromThread().AddRaw(&OutboundStatTracker, &TClientTransferStatTracker<FOutboundTransferStatistics>::OnTransferUpdatedFromThread);
			Statistics->OnInboundTransferUpdatedFromThread().AddRaw(&InboundStatTracker, &TClientTransferStatTracker<FInboundTransferStatistics>::OnTransferUpdatedFromThread);
			TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FClientTransferStatisticsModel::Tick));
		}
	}

	FClientTransferStatisticsModel::~FClientTransferStatisticsModel()
	{
		if (INetworkMessagingExtension* Statistics = Private::ClientTransferStatisticsModel::GetMessagingStatistics())
		{
			Statistics->OnOutboundTransferUpdatedFromThread().RemoveAll(&OutboundStatTracker);
			Statistics->OnInboundTransferUpdatedFromThread().RemoveAll(&InboundStatTracker);
		}
		
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}

	const TArray<FConcertTransferSamplePoint>& FClientTransferStatisticsModel::GetTransferStatTimeline(EConcertTransferStatistic StatisticType) const
	{
		switch (StatisticType)
		{
		case EConcertTransferStatistic::SentToClient: return OutboundStatTracker.GetTransferStatisticsTimeline();
		case EConcertTransferStatistic::ReceivedFromClient: return InboundStatTracker.GetTransferStatisticsTimeline();;
		case EConcertTransferStatistic::Count:
		default:
			checkNoEntry();
			return OutboundStatTracker.GetTransferStatisticsTimeline();
		}
	}

	FOnTransferTimelineUpdated& FClientTransferStatisticsModel::OnTransferTimelineUpdated(EConcertTransferStatistic StatisticType)
	{
		switch (StatisticType)
		{
		case EConcertTransferStatistic::SentToClient: return OutboundStatTracker.GetOnTimelineUpdatedDelegates();
		case EConcertTransferStatistic::ReceivedFromClient: return InboundStatTracker.GetOnTimelineUpdatedDelegates();;
		case EConcertTransferStatistic::Count:
		default:
			checkNoEntry();
			return OutboundStatTracker.GetOnTimelineUpdatedDelegates();
		}
	}

	bool FClientTransferStatisticsModel::IsSentToClient(const FOutboundTransferStatistics& Item) const
	{
		if (const INetworkMessagingExtension* Statistics = Private::ClientTransferStatisticsModel::GetMessagingStatistics())
		{
			const FGuid ClientNodeId = Statistics->GetNodeIdFromAddress(ClientAddress);
			return ClientNodeId == Item.DestinationId;
		}
		return false;
	}

	bool FClientTransferStatisticsModel::IsReceivedFromClient(const FInboundTransferStatistics& Item) const
	{
		if (const INetworkMessagingExtension* Statistics = Private::ClientTransferStatisticsModel::GetMessagingStatistics())
		{
			const FGuid ClientNodeId = Statistics->GetNodeIdFromAddress(ClientAddress);
			return ClientNodeId == Item.OriginId;
		}
		return false; 
	}
	
	bool FClientTransferStatisticsModel::Tick(float DeltaTime)
	{
		OutboundStatTracker.Tick();
		InboundStatTracker.Tick();
		return true;
	}
}