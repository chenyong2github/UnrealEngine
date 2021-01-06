// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"

namespace Electra
{
	struct Configuration
	{	
		TMap<FString, bool>	EnabledAnalyticsEvents;
	};

	//! Initializes core service functionality. Memory hooks must have been registered before calling this function.
	bool Startup(const Configuration& Configuration);

	//! Shuts down core services.
	void Shutdown(void);

	//! Waits until all player instances have terminated, which may happen asynchronously.
	void WaitForAllPlayersToHaveTerminated();


	void AddActivePlayerInstance();
	void RemoveActivePlayerInstance();

	//! Check if an analytics event is enabled
	bool IsAnalyticsEventEnabled(const FString& AnalyticsEventName);

};

