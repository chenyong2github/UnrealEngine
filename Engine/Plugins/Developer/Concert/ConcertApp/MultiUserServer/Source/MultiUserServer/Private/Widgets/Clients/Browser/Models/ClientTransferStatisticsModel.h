// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IClientTransferStatisticsModel.h"
#include "IMessageContext.h"
#include "Containers/SpscQueue.h"
#include "Containers/Ticker.h"

namespace UE::MultiUserServer
{
	struct FTransferStatItem : FTransferStatistics
	{
		FDateTime LocalTime;

		FTransferStatItem(const FDateTime& LocalTime, const FTransferStatistics& Data)
			: FTransferStatistics(Data)
			, LocalTime(LocalTime)
		{}
	};
	
	class FClientTransferStatisticsModel : public IClientTransferStatisticsModel
	{
	public:
		
		FClientTransferStatisticsModel(const FMessageAddress& ClientAddress);
		virtual ~FClientTransferStatisticsModel() override;
		
		virtual const TArray<FConcertTransferSamplePoint>& GetTransferStatTimeline(EConcertTransferStatistic StatisticType) const override { return TransferStatisticsTimelines[StatisticType]; }
		virtual const TArray<TSharedPtr<FTransferStatistics>>& GetTransferStatsGroupedById() const override { return TransferStatisticsGroupedById; }
		virtual FOnTransferTimelineUpdated& OnTransferTimelineUpdated(EConcertTransferStatistic StatisticType) override { return OnTimelineUpdatedDelegates[StatisticType]; }
		virtual FOnTransferGroupsUpdated& OnTransferGroupsUpdated() override { return OnGroupsUpdatedDelegate; }

	private:

		using FConcertStatTypeFlags = TBitArray<TInlineAllocator<static_cast<int32>(EConcertTransferStatistic::Count)>>;
		using FMessageId = decltype(FTransferStatistics::MessageId);
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
		TArray<TSharedPtr<FTransferStatistics>> TransferStatisticsGroupedById;
		TMap<FMessageId, FDateTime> LastUpdateGroupUpdates;
		
		// Delegate info
		TMap<EConcertTransferStatistic, FOnTransferTimelineUpdated> OnTimelineUpdatedDelegates;
		FOnTransferGroupsUpdated OnGroupsUpdatedDelegate;
		FTSTicker::FDelegateHandle TickHandle;
		
		void OnTransferUpdatedFromThread(FTransferStatistics TransferStatistics);
		bool AreRelevantStats(const FTransferStatistics& TransferStatistics) const;
		bool IsSentToClient(const FTransferStatistics& Item) const;
		bool IsReceivedFromClient(const FTransferStatistics& Item) const;
		
		bool Tick(float DeltaTime);
		
		void RemoveOldStatTimelines(FConcertStatTypeFlags& ModifiedTimelines);
		bool RemoveOldGroupedStats();
		
		void UpdateStatTimeline(const FTransferStatItem& TransferStatistics, FConcertTransferSamplePoint& SamplingThisTick);
		void UpdateGroupedStats(const FTransferStatItem& TransferStatistics);
	};
}

