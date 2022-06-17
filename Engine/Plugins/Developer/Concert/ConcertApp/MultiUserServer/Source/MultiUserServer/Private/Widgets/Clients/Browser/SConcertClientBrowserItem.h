// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMessageContext.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;

namespace UE::MultiUserServer
{
	class SClientNetworkStats;
	class IClientNetworkStatisticsModel;
	struct FClientBrowserItem;

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

	private:

		/** What we're displaying */
		TSharedPtr<FClientBrowserItem> Item;
		TSharedPtr<IClientNetworkStatisticsModel> StatModel;

		/** The text to highlight */
		TSharedPtr<FText> HighlightText;

		TSharedPtr<STextBlock> ClientName;
		TSharedPtr<SClientNetworkStats> NetworkStats;
		TSharedPtr<STextBlock> ClientIP4;

		TSharedRef<SWidget> CreateHeader();
		TSharedRef<SWidget> CreateStats();
		TSharedRef<SWidget> CreateFooter();

		const FSlateBrush* GetBackgroundImage() const;
	};
}
