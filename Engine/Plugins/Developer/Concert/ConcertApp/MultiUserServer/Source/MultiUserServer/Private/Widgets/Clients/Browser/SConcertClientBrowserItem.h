// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMessageContext.h"
#include "Models/IClientTransferStatisticsModel.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FMenuBuilder;
class STextBlock;

namespace UE::MultiUserServer
{
	class SClientNetworkStats;
	class IClientNetworkStatisticsModel;
	struct FClientBrowserItem;

	enum class EClientDisplayMode : uint8
	{
		/** Displays the sent and read packets */
		NetworkGraph = 0,
		/** Displays a table showing MessageId, Sent, Acked, and Size updated in realtime for outbound UDP segments. */
		OutboundSegementTable = 1,
		/** Displays a table showing MessageId, Received, and Size updated in realtime for inbound UDP segments. */
		InboundSegmentTable = 2
	};
	
	class SConcertClientBrowserItem : public SCompoundWidget
	{
	public:

		SLATE_BEGIN_ARGS(SConcertClientBrowserItem) {}
			SLATE_ARGUMENT(TSharedPtr<FText>, HighlightText)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, TSharedRef<FClientBrowserItem> InClientItem, TSharedRef<IClientNetworkStatisticsModel> InStatModel);

		FString GetClientDisplayName() const;
		
		/** Appends the statistics to the search terms */
		void AppendSearchTerms(TArray<FString>& SearchTerms) const;

		void SetDisplayMode(EClientDisplayMode Value) { DisplayMode = Value; }
		EClientDisplayMode GetDisplayMode() const { return DisplayMode;}

	private:

		/** What we're displaying */
		TSharedPtr<FClientBrowserItem> Item;
		TSharedPtr<IClientNetworkStatisticsModel> StatModel;
		TSharedPtr<IClientTransferStatisticsModel> TransferStatsModel;

		/** The text to highlight */
		TSharedPtr<FText> HighlightText;

		TSharedPtr<STextBlock> ClientName;
		TSharedPtr<SClientNetworkStats> NetworkStats;
		TSharedPtr<STextBlock> ClientIP4;

		EClientDisplayMode DisplayMode = EClientDisplayMode::NetworkGraph;

		TSharedRef<SWidget> CreateHeader();
		TSharedRef<SWidget> CreateContentArea();
		TSharedRef<SWidget> CreateStats();
		TSharedRef<SWidget> CreateFooter();

		const FSlateBrush* GetBackgroundImage() const;
	};
}
