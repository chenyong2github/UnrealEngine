// Copyright Epic Games, Inc. All Rights Reserved.

#include "SClientNetworkStats.h"

#include "ConcertServerStyle.h"
#include "INetworkMessagingExtension.h"
#include "Math/UnitConversion.h"
#include "Widgets/Clients/Browser/Models/ClientNetworkStatisticsModel.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI.SClientNetworkStats"

namespace UE::MultiUserServer
{
	void SClientNetworkStats::Construct(const FArguments& InArgs, const FMessageAddress& InNodeAddress, TSharedRef<IClientNetworkStatisticsModel> InStatisticModel)
	{
		NodeAddress = InNodeAddress;
		StatisticModel = MoveTemp(InStatisticModel);
		
		HighlightText = InArgs._HighlightText;
		check(HighlightText);
		
		ChildSlot
		.HAlign(HAlign_Fill)
		[
			CreateStatTable()
		];
	}

	void SClientNetworkStats::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime,const float InDeltaTime)
	{
		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

		if (const TOptional<FMessageTransportStatistics> Stats = StatisticModel->GetLatestNetworkStatistics(NodeAddress))
		{
			UpdateStatistics(*Stats);
		}
		else
		{
			const FText NotApplicableText = FText::FromString(TEXT("n/a"));
			SendText->SetText(NotApplicableText);
			ReceiveText->SetText(NotApplicableText);
			RoundTripTimeText->SetText(NotApplicableText);
			InflightText->SetText(NotApplicableText);
			LossText->SetText(NotApplicableText);
		}
	}

	void SClientNetworkStats::AppendSearchTerms(TArray<FString>& SearchTerms) const
	{
		SearchTerms.Add(SendText->GetText().ToString());
		SearchTerms.Add(ReceiveText->GetText().ToString());
		SearchTerms.Add(RoundTripTimeText->GetText().ToString());
		SearchTerms.Add(InflightText->GetText().ToString());
		SearchTerms.Add(LossText->GetText().ToString());
	}

	void SClientNetworkStats::UpdateStatistics(const FMessageTransportStatistics& Statistics)
	{
		// Send
		{
			const FNumericUnit<uint64> Unit = FUnitConversion::QuantizeUnitsToBestFit(Statistics.TotalBytesSent, EUnit::Bytes);
			const FString DisplayString = FString::Printf(TEXT("%lld %s"), Unit.Value, FUnitConversion::GetUnitDisplayString(Unit.Units));
			SendText->SetText(FText::FromString(DisplayString));
		}
		// Receive
		{
			const FNumericUnit<uint64> Unit = FUnitConversion::QuantizeUnitsToBestFit(Statistics.TotalBytesReceived, EUnit::Bytes);
			const FString DisplayString = FString::Printf(TEXT("%lld %s"), Statistics.TotalBytesReceived, FUnitConversion::GetUnitDisplayString(Unit.Units));
			ReceiveText->SetText(FText::FromString(DisplayString));
		}
		// RTT
		{
			const FNumericUnit<uint64> Unit = FUnitConversion::QuantizeUnitsToBestFit(static_cast<uint64>(Statistics.AverageRTT.GetTotalMilliseconds()), EUnit::Milliseconds);
			const FString DisplayString = FString::Printf(TEXT("%lld %s"), Unit.Value, FUnitConversion::GetUnitDisplayString(Unit.Units));
			RoundTripTimeText->SetText(FText::FromString(DisplayString));
		}
		// Inflight
		{
			const FNumericUnit<uint64> Unit = FUnitConversion::QuantizeUnitsToBestFit(Statistics.BytesInflight, EUnit::Bytes);
			const FString DisplayString = FString::Printf(TEXT("%lld %s"), Statistics.BytesInflight, FUnitConversion::GetUnitDisplayString(Unit.Units));
			InflightText->SetText(FText::FromString(DisplayString));
		}
		// Loss
		{
			const FNumericUnit<uint64> Unit = FUnitConversion::QuantizeUnitsToBestFit(Statistics.TotalBytesLost, EUnit::Bytes);
			const FString DisplayString = FString::Printf(TEXT("%lld %s"), Statistics.TotalBytesLost, FUnitConversion::GetUnitDisplayString(Unit.Units));
			LossText->SetText(FText::FromString(DisplayString));
		}
	}

	TSharedRef<SWidget> SClientNetworkStats::CreateStatTable()
	{
		const TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox)
			.ToolTipText_Lambda([this]()
			{
				const TOptional<FMessageTransportStatistics> Stats = StatisticModel->GetLatestNetworkStatistics(NodeAddress);
				return Stats.IsSet() ? FText::GetEmpty() : LOCTEXT("ErrorGettingStats", "Error getting stats");
			});
		AddStatistic(Content, LOCTEXT("SentLabel", "Sent"), LOCTEXT("SentTooltip", "Total bytes sent to this client"), SendText);
		AddStatistic(Content, LOCTEXT("ReceiveLabel", "Received"), LOCTEXT("ReceiveTooltip", "Total bytes received from this client"), ReceiveText);
		AddStatistic(Content, LOCTEXT("RttLabel", "RTT"), LOCTEXT("RttTooltip", "Round trip time"), RoundTripTimeText);
		AddStatistic(Content, LOCTEXT("InflightLabel", "Inflight"), LOCTEXT("InflightTooltip", "Total reliable bytes awaiting an ack from client"), InflightText);

		// Loss should be at the right
		SHorizontalBox::FScopedWidgetSlotArguments LossSlat = Content->AddSlot();
		AddStatistic(LossSlat, LOCTEXT("LossLabel", "Lost"), LOCTEXT("LostTooltip", "Total bytes lost while sending to the client"), LossText);
		LossSlat.HAlign(HAlign_Right);
		LossSlat.FillWidth(1.f);
		
		return Content;
	}
	
	void SClientNetworkStats::AddStatistic(const TSharedRef<SHorizontalBox>& AddTo, const FText& StatisticName, const FText& StatisticToolTip, TSharedPtr<STextBlock>& AssignTo)
	{
		SHorizontalBox::FScopedWidgetSlotArguments Slot = AddTo->AddSlot();
		AddStatistic(Slot, StatisticName, StatisticToolTip, AssignTo);
	}

	void SClientNetworkStats::AddStatistic(SHorizontalBox::FScopedWidgetSlotArguments& Slot, const FText& StatisticName, const FText& StatisticToolTip, TSharedPtr<STextBlock>& AssignTo)
	{
		Slot
		.AutoWidth()
		.Padding(3.f)
		[
			SNew(SVerticalBox)
			.ToolTipText(StatisticToolTip)

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(StatisticName)
				.ColorAndOpacity(FColor::White)
			]

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(AssignTo, STextBlock)
				.ColorAndOpacity(FColor::White)
				.HighlightText_Lambda([this](){ return *HighlightText; })
			]
		];
	}
}

#undef LOCTEXT_NAMESPACE