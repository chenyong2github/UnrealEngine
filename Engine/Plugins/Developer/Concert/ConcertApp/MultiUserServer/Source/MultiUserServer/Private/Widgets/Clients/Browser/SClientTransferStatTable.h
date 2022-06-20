// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class ITableRow;
template<typename T> class SListView;
class STableViewBase;
struct FTransferStatistics;

namespace UE::MultiUserServer
{
	class IClientTransferStatisticsModel;

	/**
	 * Displays FTransferStatistics as they are updated by INetworkMessagingExtension.
	 */
	class SClientTransferStatTable : public SCompoundWidget
	{
	public:

		SLATE_BEGIN_ARGS(SClientTransferStatTable)
		{}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, TSharedRef<IClientTransferStatisticsModel> InStatsModel);

	private:

		/** Tells us when the transfer stats have changed */
		TSharedPtr<IClientTransferStatisticsModel> StatsModel;
		/** Displays the transfer stats */
		TSharedPtr<SListView<TSharedPtr<FTransferStatistics>>> SegmenterListView;

		TSharedRef<ITableRow> OnGenerateActivityRowWidget(TSharedPtr<FTransferStatistics> InStats, const TSharedRef<STableViewBase>& OwnerTable) const;
		void OnTransferStatisticsUpdated() const;
	};
}


