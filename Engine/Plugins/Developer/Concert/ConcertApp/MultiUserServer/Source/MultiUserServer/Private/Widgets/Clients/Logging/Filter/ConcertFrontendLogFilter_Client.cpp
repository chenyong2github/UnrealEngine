// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertFrontendLogFilter_Client.h"

#include "ConcertTransportEvents.h"
#include "Widgets/Clients/Logging/ConcertLogEntry.h"

bool FConcertLogFilter_Client::PassesFilter(const FConcertLogEntry& InItem) const
{
	return AllowedClientEndpointIds.Contains(InItem.Log.OriginEndpointId)
		|| AllowedClientEndpointIds.Contains(InItem.Log.DestinationEndpointId);
}

void FConcertLogFilter_Client::AllowOnly(const FGuid& ClientEndpointId)
{
	if (AllowedClientEndpointIds.Num() > 1 || !AllowedClientEndpointIds.Contains(ClientEndpointId))
	{
		AllowedClientEndpointIds = { ClientEndpointId };
		OnChanged().Broadcast();
	}
}
