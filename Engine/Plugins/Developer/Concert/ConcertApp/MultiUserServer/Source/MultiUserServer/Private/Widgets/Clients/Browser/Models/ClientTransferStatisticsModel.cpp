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
	{
		for (int32 i = 0; i < static_cast<int32>(EConcertTransferStatistic::Count); ++i)
		{
			const EConcertTransferStatistic StatType = static_cast<EConcertTransferStatistic>(i);
			TransferStatisticsTimelines.Add(StatType);
			OnTimelineUpdatedDelegates.Add(StatType);
		}
		
		if (INetworkMessagingExtension* Statistics = Private::ClientTransferStatisticsModel::GetMessagingStatistics())
		{
			Statistics->OnOutboundTransferUpdatedFromThread().AddRaw(this, &FClientTransferStatisticsModel::OnTransferUpdatedFromThread);
			TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FClientTransferStatisticsModel::Tick));
		}
	}

	FClientTransferStatisticsModel::~FClientTransferStatisticsModel()
	{
		if (INetworkMessagingExtension* Statistics = Private::ClientTransferStatisticsModel::GetMessagingStatistics())
		{
			Statistics->OnOutboundTransferUpdatedFromThread().RemoveAll(this);
		}
		
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}

	void FClientTransferStatisticsModel::OnTransferUpdatedFromThread(FOutboundTransferStatistics TransferStatistics)
	{
		if (AreRelevantStats(TransferStatistics))
		{
			AsyncStatQueue.Enqueue(FTransferStatItem{ FDateTime::Now(), TransferStatistics });
		}
	}

	bool FClientTransferStatisticsModel::AreRelevantStats(const FOutboundTransferStatistics& TransferStatistics) const
	{
		return IsSentToClient(TransferStatistics) || IsReceivedFromClient(TransferStatistics);
	}

	bool FClientTransferStatisticsModel::IsSentToClient(const FOutboundTransferStatistics& Item) const
	{
		if (INetworkMessagingExtension* Statistics = Private::ClientTransferStatisticsModel::GetMessagingStatistics())
		{
			const FGuid ClientNodeId = Statistics->GetNodeIdFromAddress(ClientAddress);
			return ClientNodeId == Item.DestinationId;
		}
		return false;
	}

	bool FClientTransferStatisticsModel::IsReceivedFromClient(const FOutboundTransferStatistics& Item) const
	{
		// TODO: UDP module is missing a way to find out
		return false; 
	}
	
	bool FClientTransferStatisticsModel::Tick(float DeltaTime)
	{
		constexpr int32 NumStats = static_cast<int32>(EConcertTransferStatistic::Count);
		FConcertStatTypeFlags ConcertStatFlags(false, NumStats);

		RemoveOldStatTimelines(ConcertStatFlags);
		const bool bRemovedAnyGroups = RemoveOldGroupedStats();
		
		const FDateTime Now = FDateTime::Now();
		FConcertTransferSamplePoint SamplingThisTick[NumStats] = { FConcertTransferSamplePoint{ Now }, FConcertTransferSamplePoint{ Now } };
		static_assert(NumStats == 2);
		while (const TOptional<FTransferStatItem> TransferStatistics = AsyncStatQueue.Dequeue())
		{
			constexpr int32 StatAsInt = static_cast<int32>(EConcertTransferStatistic::SentToClient);
			ConcertStatFlags[StatAsInt] = true;
			
			UpdateStatTimeline(TransferStatistics.GetValue(), SamplingThisTick[StatAsInt]);
			UpdateGroupedStats(TransferStatistics.GetValue());
		}

		if (bRemovedAnyGroups || ConcertStatFlags.CountSetBits() != 0)
		{
			for (int32 i = 0; i < NumStats; ++i)
			{
				const bool bHadStatThisUpdate = ConcertStatFlags[i]; 
				if (bHadStatThisUpdate)
				{
					const EConcertTransferStatistic Stat = static_cast<EConcertTransferStatistic>(i);
					TransferStatisticsTimelines[Stat].Add(SamplingThisTick[i]);
					OnTimelineUpdatedDelegates[Stat].Broadcast();
				}
			}
			OnGroupsUpdatedDelegate.Broadcast();
		}

		return true;
	}

	namespace Private::ClientTransferStatisticsModel
	{
		bool IsTooOld(const FDateTime& StatCreationTime, const FDateTime& Now)
		{
			const FTimespan RetainTime = FTimespan::FromSeconds(60.f);
			return RetainTime < Now - StatCreationTime;
		}
	}
	
	void FClientTransferStatisticsModel::RemoveOldStatTimelines(FConcertStatTypeFlags& ModifiedTimelines)
	{
		const FDateTime Now = FDateTime::Now();
		for (auto TimelineIt = TransferStatisticsTimelines.CreateIterator(); TimelineIt; ++TimelineIt)
		{
			TArray<FConcertTransferSamplePoint>& SamplePoints = TimelineIt->Value;
			bool bModifiedAny = false;
			for (auto SampleIt = SamplePoints.CreateIterator(); SampleIt; ++SampleIt)
			{
				if (Private::ClientTransferStatisticsModel::IsTooOld(SampleIt->LocalTime, Now))
				{
					SampleIt.RemoveCurrent();
					bModifiedAny = true;
				}
			}

			const int32 Index = static_cast<int32>(TimelineIt->Key);
			ModifiedTimelines[Index] = bModifiedAny;
		}

		for (auto IncompleteIt = IncompleteStatsUntilNow.CreateIterator(); IncompleteIt; ++IncompleteIt)
		{
			if (Private::ClientTransferStatisticsModel::IsTooOld(IncompleteIt->Value.LastUpdate, Now))
			{
				IncompleteIt.RemoveCurrent();
			}
		}
	}

	bool FClientTransferStatisticsModel::RemoveOldGroupedStats()
	{
		bool bRemovedAny = false;
		const FDateTime Now = FDateTime::Now();
		for (auto It = TransferStatisticsGroupedById.CreateIterator(); It; ++It)
		{
			const TSharedPtr<FOutboundTransferStatistics>& Stat = *It;
			if (Private::ClientTransferStatisticsModel::IsTooOld(LastUpdateGroupUpdates[Stat->MessageId], Now))
			{
				LastUpdateGroupUpdates.Remove(Stat->MessageId);
				It.RemoveCurrent();
				bRemovedAny = true;
			}
		}
		return bRemovedAny;
	}

	void FClientTransferStatisticsModel::UpdateStatTimeline(const FTransferStatItem& TransferStatistics, FConcertTransferSamplePoint& SamplingThisTick)
	{
		uint64 BytesSentSinceLastTime = TransferStatistics.BytesSent;
		// Same message ID may have been queued multiple times
		if (FIncompleteMessageData* ItemThisTick = IncompleteStatsUntilNow.Find(TransferStatistics.MessageId))
		{
			BytesSentSinceLastTime -= ItemThisTick->BytesTransferredSoFar;
		}

		IncompleteStatsUntilNow.Add(TransferStatistics.MessageId, { TransferStatistics.BytesSent, FDateTime::Now() });
		SamplingThisTick.BytesTransferred += BytesSentSinceLastTime;
	}

	void FClientTransferStatisticsModel::UpdateGroupedStats(const FTransferStatItem& TransferStatistics)
	{
		const TSharedPtr<FOutboundTransferStatistics> NewValue = MakeShared<FOutboundTransferStatistics>(TransferStatistics);
		const auto Pos = Algo::LowerBound(TransferStatisticsGroupedById, NewValue, [](const TSharedPtr<FOutboundTransferStatistics>& Value, const TSharedPtr<FOutboundTransferStatistics>& Check)
		{
			return Value->MessageId < Check->MessageId;
		});
		if (TransferStatisticsGroupedById.Num() > 0 && Pos < TransferStatisticsGroupedById.Num() && TransferStatisticsGroupedById[Pos] && TransferStatisticsGroupedById[Pos]->MessageId == NewValue->MessageId)
		{
			*TransferStatisticsGroupedById[Pos] = *NewValue;
		}
		else
		{
			TransferStatisticsGroupedById.Insert(NewValue, Pos);
		}

		LastUpdateGroupUpdates.Add(NewValue->MessageId, FDateTime::Now());
	}
}
