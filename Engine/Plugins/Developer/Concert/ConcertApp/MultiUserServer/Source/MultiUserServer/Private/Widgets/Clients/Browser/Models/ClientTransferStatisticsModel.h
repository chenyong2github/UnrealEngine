// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IClientTransferStatisticsModel.h"
#include "Containers/SpscQueue.h"
#include "Containers/Ticker.h"

namespace UE::MultiUserServer
{
	class FClientTransferStatisticsModel : public IClientTransferStatisticsModel
	{
	public:
		
		FClientTransferStatisticsModel(const FMessageAddress& MessageAddress);
		virtual ~FClientTransferStatisticsModel() override;

		virtual const TArray<TSharedPtr<FTransferStatistics>>& GetSortedTransferStatistics() const override { return Stats; }
		virtual FOnTransferStatisticsUpdated& OnTransferStatisticsUpdated() override { return OnUpdatedDelegate; }

	private:

		const FMessageAddress MessageAddress;

		TSpscQueue<FTransferStatistics> AsyncStatQueue;
		TArray<TSharedPtr<FTransferStatistics>> Stats;
		
		FOnTransferStatisticsUpdated OnUpdatedDelegate;
		FTSTicker::FDelegateHandle TickHandle;
		
		void OnTransferUpdatedFromThread(FTransferStatistics TransferStatistics);
		bool Tick(float DeltaTime);
	};
}

