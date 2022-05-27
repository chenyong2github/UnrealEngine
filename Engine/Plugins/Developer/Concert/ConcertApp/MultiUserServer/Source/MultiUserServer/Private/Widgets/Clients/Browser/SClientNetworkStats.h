// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMessageContext.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;
struct FMessageTransportStatistics;

namespace UE::MultiUserServer
{
	class IClientNetworkStatisticsModel;
	
	/**
	 * Displays statistics about a client connection in a table like format: send, receive, RTT, Inflight, and Loss.
	 */
	class SClientNetworkStats : public SCompoundWidget
	{
	public:

		SLATE_BEGIN_ARGS(SClientNetworkStats){}
			SLATE_ARGUMENT(TSharedPtr<FText>, HighlightText)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const FMessageAddress& InNodeAddress, TSharedRef<IClientNetworkStatisticsModel> InStatisticModel);

		//~ Begin SCompoundWidget Interface		
		virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
		//~ End SCompoundWidget Interface
		
		/** Appends the statistics to the search terms */
		void AppendSearchTerms(TArray<FString>& SearchTerms) const;

	private:

		/** The ID getting visualised */
		FMessageAddress NodeAddress;
		/** Used to obtain settings */
		TSharedPtr<IClientNetworkStatisticsModel> StatisticModel;
		
		/** The text to highlight */
		TSharedPtr<FText> HighlightText;

		TSharedPtr<STextBlock> SendText;
		TSharedPtr<STextBlock> ReceiveText;
		TSharedPtr<STextBlock> RoundTripTimeText;
		TSharedPtr<STextBlock> InflightText;
		TSharedPtr<STextBlock> LossText;
		
		void UpdateStatistics(const FMessageTransportStatistics& Statistics);

		TSharedRef<SWidget> CreateStatTable();
		void AddStatistic(const TSharedRef<SHorizontalBox>& AddTo, const FText& StatisticName, const FText& StatisticToolTip, TSharedPtr<STextBlock>& AssignTo);
		void AddStatistic(SHorizontalBox::FScopedWidgetSlotArguments& Slot, const FText& StatisticName, const FText& StatisticToolTip, TSharedPtr<STextBlock>& AssignTo);
	};
}

