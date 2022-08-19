// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FMessageTransportStatistics;
struct FMessageAddress;

namespace UE::MultiUserServer
{
	class IClientNetworkStatisticsModel;
	class IClientTransferStatisticsModel;

	enum class EConcertBrowserItemDisplayMode : uint8
	{
		/** Displays the sent and read packets */
		NetworkGraph = 0,
		/** Displays a table showing MessageId, Sent, Acked, and Size updated in realtime for outbound UDP segments. */
		OutboundSegementTable = 1,
		/** Displays a table showing MessageId, Received, and Size updated in realtime for inbound UDP segments. */
		InboundSegmentTable = 2
	};

	/** Implemented by items that can be displayed in the Concert Network Browser */
	class IConcertBrowserItem
	{
	public:

		/** The title of the thumbnail */
		virtual FString GetDisplayName() const = 0;
		/** What to display when the thumbnail is hovered */
		virtual FText GetToolTip() const = 0;
		/** The address of the corresponding item. */
		virtual FMessageAddress GetMessageAddress() const = 0;
		
		virtual void SetDisplayMode(EConcertBrowserItemDisplayMode Value) = 0;
		virtual EConcertBrowserItemDisplayMode GetDisplayMode() const = 0;

		/** The network statistics for this item */
		virtual TSharedRef<IClientTransferStatisticsModel> GetTransferStatistics() const = 0;

		/** Adds terms this item can be searched by */
		virtual void AppendSearchTerms(TArray<FString>& SearchTerms) const = 0;

		/** Gets the latest network statistics for this item if they are available; most likely unavailable when IsOnline returns false. */
		virtual TOptional<FMessageTransportStatistics> GetLatestNetworkStatistics() const = 0;
		/** Whether this client is currently reachable */
		virtual bool IsOnline() const = 0;

		virtual ~IConcertBrowserItem() = default;
	};

	/** Util for implementing shared logic. */
	class FConcertBrowserItemCommonImpl : public IConcertBrowserItem
	{
	public:

		FConcertBrowserItemCommonImpl(TSharedRef<IClientNetworkStatisticsModel> NetworkStatisticsModel);

		virtual void SetDisplayMode(EConcertBrowserItemDisplayMode Value) override;
		virtual EConcertBrowserItemDisplayMode GetDisplayMode() const override;
		virtual void AppendSearchTerms(TArray<FString>& SearchTerms) const override;
		virtual TOptional<FMessageTransportStatistics> GetLatestNetworkStatistics() const override;
		virtual bool IsOnline() const override;

	private:

		TSharedRef<IClientNetworkStatisticsModel> NetworkStatisticsModel;
		EConcertBrowserItemDisplayMode ItemDisplayMode = EConcertBrowserItemDisplayMode::NetworkGraph;
	};
}
