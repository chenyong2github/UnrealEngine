// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TransferStatisticsModel.h"
#include "Widgets/Clients/Browser/Models/ITransferStatisticsModel.h"

namespace UE::MultiUserServer
{
	class FServerTransferStatisticsModel : public FTransferStatisticsModelBase
	{
	protected:
		
		virtual bool ShouldIncludeOutboundStat(const FOutboundTransferStatistics& Item) const override { return true; }
		virtual bool ShouldIncludeInboundStat(const FInboundTransferStatistics& Item) const override { return true; }
	};
}

