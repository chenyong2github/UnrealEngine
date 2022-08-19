// Copyright Epic Games, Inc. All Rights Reserved.

#include "TransferStatisticsModel.h"

#include "INetworkMessagingExtension.h"
#include "Containers/Ticker.h"
#include "Features/IModularFeatures.h"

namespace UE::MultiUserServer
{
	namespace Private::TransferStatisticsModel
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

	FTransferStatisticsModelBase::FTransferStatisticsModelBase()
		: OutboundStatTracker([this](const FOutboundTransferStatistics& Stats) { return ShouldIncludeOutboundStat(Stats); }, [](const FOutboundTransferStatistics& Stats){ return Stats.BytesSent; })
		, InboundStatTracker([this](const FInboundTransferStatistics& Stats) { return ShouldIncludeInboundStat(Stats); }, [](const FInboundTransferStatistics& Stats){ return Stats.BytesReceived; })
	{
		if (INetworkMessagingExtension* Statistics = Private::TransferStatisticsModel::GetMessagingStatistics())
		{
			Statistics->OnOutboundTransferUpdatedFromThread().AddRaw(&OutboundStatTracker, &TClientTransferStatTracker<FOutboundTransferStatistics>::OnTransferUpdatedFromThread);
			Statistics->OnInboundTransferUpdatedFromThread().AddRaw(&InboundStatTracker, &TClientTransferStatTracker<FInboundTransferStatistics>::OnTransferUpdatedFromThread);
			TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FTransferStatisticsModelBase::Tick));
		}
	}

	FTransferStatisticsModelBase::~FTransferStatisticsModelBase()
	{
		if (INetworkMessagingExtension* Statistics = Private::TransferStatisticsModel::GetMessagingStatistics())
		{
			Statistics->OnOutboundTransferUpdatedFromThread().RemoveAll(&OutboundStatTracker);
			Statistics->OnInboundTransferUpdatedFromThread().RemoveAll(&InboundStatTracker);
		}
		
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}

	const TArray<FConcertTransferSamplePoint>& FTransferStatisticsModelBase::GetTransferStatTimeline(EConcertTransferStatistic StatisticType) const
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

	FOnTransferTimelineUpdated& FTransferStatisticsModelBase::OnTransferTimelineUpdated(EConcertTransferStatistic StatisticType)
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
	
	bool FTransferStatisticsModelBase::Tick(float DeltaTime)
	{
		OutboundStatTracker.Tick();
		InboundStatTracker.Tick();
		return true;
	}
}