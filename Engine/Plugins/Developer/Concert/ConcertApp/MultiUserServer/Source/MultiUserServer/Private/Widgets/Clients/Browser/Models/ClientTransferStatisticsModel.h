// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IClientTransferStatisticsModel.h"
#include "IMessageContext.h"
#include "Containers/SpscQueue.h"
#include "Containers/Ticker.h"

namespace UE::MultiUserServer
{
	struct FTransferStatItem : FOutboundTransferStatistics
	{
		FDateTime LocalTime;

		FTransferStatItem(const FDateTime& LocalTime, const FOutboundTransferStatistics& Data)
			: FOutboundTransferStatistics(Data)
			, LocalTime(LocalTime)
		{}
	};
	
	class FClientTransferStatisticsModel : public IClientTransferStatisticsModel
	{
	public:
		
		FClientTransferStatisticsModel(const FMessageAddress& ClientAddress);
		virtual ~FClientTransferStatisticsModel() override;
		
		virtual const TArray<FConcertTransferSamplePoint>& GetTransferStatTimeline(EConcertTransferStatistic StatisticType) const override { return TransferStatisticsTimelines[StatisticType]; }
		virtual const TArray<TSharedPtr<FOutboundTransferStatistics>>& GetTransferStatsGroupedById() const override { return TransferStatisticsGroupedById; }
		virtual FOnTransferTimelineUpdated& OnTransferTimelineUpdated(EConcertTransferStatistic StatisticType) override { return OnTimelineUpdatedDelegates[StatisticType]; }
		virtual FOnTransferGroupsUpdated& OnTransferGroupsUpdated() override { return OnGroupsUpdatedDelegate; }

	private:

		using FConcertStatTypeFlags = TBitArray<TInlineAllocator<static_cast<int32>(EConcertTransferStatistic::Count)>>;
		using FMessageId = decltype(FOutboundTransferStatistics::MessageId);
		struct FIncompleteMessageData
		{
			uint64 BytesTransferredSoFar;
			FDateTime LastUpdate;
		};

		const FMessageAddress ClientAddress;
		TSpscQueue<FTransferStatItem> AsyncStatQueue;

		// Timelines
		TMap<EConcertTransferStatistic, TArray<FConcertTransferSamplePoint>> TransferStatisticsTimelines;
		TMap<FMessageId, FIncompleteMessageData> IncompleteStatsUntilNow;

		// Grouped
		TArray<TSharedPtr<FOutboundTransferStatistics>> TransferStatisticsGroupedById;
		TMap<FMessageId, FDateTime> LastUpdateGroupUpdates;
		
		// Delegate info
		TMap<EConcertTransferStatistic, FOnTransferTimelineUpdated> OnTimelineUpdatedDelegates;
		FOnTransferGroupsUpdated OnGroupsUpdatedDelegate;
		FTSTicker::FDelegateHandle TickHandle;
		
		void OnTransferUpdatedFromThread(FOutboundTransferStatistics TransferStatistics);
		bool AreRelevantStats(const FOutboundTransferStatistics& TransferStatistics) const;
		bool IsSentToClient(const FOutboundTransferStatistics& Item) const;
		bool IsReceivedFromClient(const FOutboundTransferStatistics& Item) const;
		
		bool Tick(float DeltaTime);
		
		void RemoveOldStatTimelines(FConcertStatTypeFlags& ModifiedTimelines);
		bool RemoveOldGroupedStats();
		
		void UpdateStatTimeline(const FTransferStatItem& TransferStatistics, FConcertTransferSamplePoint& SamplingThisTick);
		void UpdateGroupedStats(const FTransferStatItem& TransferStatistics);
	};
}

