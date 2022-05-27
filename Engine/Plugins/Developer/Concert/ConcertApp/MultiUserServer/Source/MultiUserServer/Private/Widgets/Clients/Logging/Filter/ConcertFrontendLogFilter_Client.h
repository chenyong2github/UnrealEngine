// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Clients/Logging/Filter/ConcertFrontendLogFilter.h"

/** Only allows messages from the given clients */
class FConcertLogFilter_Client : public FConcertLogFilter
{
public:

	FConcertLogFilter_Client() = default;
	explicit FConcertLogFilter_Client(const FGuid& SingleAllowedId)
		: AllowedClientEndpointIds({ SingleAllowedId })
	{}

	//~ Begin FConcertLogFilter Interface
	virtual bool PassesFilter(const FConcertLog& InItem) const override;
	//~ End FConcertLogFilter Interface

	void AllowOnly(const FGuid& ClientEndpointId);

private:

	/** Messages to and from the following client endpoint IDs are allowed */
	TSet<FGuid> AllowedClientEndpointIds;
};
