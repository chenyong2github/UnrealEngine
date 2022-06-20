// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMessageContext.h"

struct FMessageAddress;
struct FTransferStatistics;

namespace UE::MultiUserServer
{
	DECLARE_MULTICAST_DELEGATE(FOnTransferStatisticsUpdated);
	
	/** Keeps track of a single client's transferstatistics */
	class IClientTransferStatisticsModel
	{
	public:

		/** Gets the transfer statistics sorted descending by message ID */
		virtual const TArray<TSharedPtr<FTransferStatistics>>& GetSortedTransferStatistics() const = 0;

		/** Called when the transfer statistics change */
		virtual FOnTransferStatisticsUpdated& OnTransferStatisticsUpdated() = 0;

		virtual ~IClientTransferStatisticsModel() = default;
	};
}


