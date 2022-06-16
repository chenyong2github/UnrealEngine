// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/Stats.h"
#include "Online/OnlineComponent.h"

namespace UE::Online {

	class ONLINESERVICESCOMMON_API FStatsCommon : public TOnlineComponent<IStats>
	{
	public:
		using Super = IStats;

		FStatsCommon(FOnlineServicesCommon& InServices);

		// TOnlineComponent
		virtual void Initialize() override;
		virtual void RegisterCommands() override;

		// IStats
		virtual TOnlineAsyncOpHandle<FUpdateStats> UpdateStats(FUpdateStats::Params&& Params) override;
		virtual TOnlineAsyncOpHandle<FQueryStats> QueryStats(FQueryStats::Params&& Params) override;
		virtual TOnlineAsyncOpHandle<FBatchQueryStats> BatchQueryStats(FBatchQueryStats::Params&& Params) override;
#if !UE_BUILD_SHIPPING
		virtual TOnlineAsyncOpHandle<FResetStats> ResetStats(FResetStats::Params&& Params) override;
#endif // !UE_BUILD_SHIPPING
	};

/* UE::Online */ }
