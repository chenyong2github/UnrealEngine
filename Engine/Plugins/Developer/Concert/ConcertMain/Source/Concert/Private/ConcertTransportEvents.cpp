// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertTransportEvents.h"

namespace ConcertTransportEvents
{
	FConcertTransportLoggingEnabledChanged& OnConcertTransportLoggingEnabledChangedEvent()
	{
		static FConcertTransportLoggingEnabledChanged Instance;
		return Instance;
	}
	
	FConcertClientLogEvent& OnConcertClientLogEvent()
	{
		static FConcertClientLogEvent Instance;
		return Instance;
	}

	FConcertServerLogEvent& OnConcertServerLogEvent()
	{
		static FConcertServerLogEvent Instance;
		return Instance;
	}
}
