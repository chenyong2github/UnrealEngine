// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertFrontendLogFilter_Client.h"

#include "ConcertTransportEvents.h"

bool FConcertLogFilter_Client::PassesFilter(const FConcertLog& InItem) const
{
	return AllowedClientEndpointIds.Contains(InItem.OriginEndpointId)
		|| AllowedClientEndpointIds.Contains(InItem.DestinationEndpointId);
}

void FConcertLogFilter_Client::AllowOnly(const FGuid& ClientEndpointId)
{
	if (AllowedClientEndpointIds.Num() > 1 || !AllowedClientEndpointIds.Contains(ClientEndpointId))
	{
		AllowedClientEndpointIds = { ClientEndpointId };
		OnChanged().Broadcast();
	}
}
