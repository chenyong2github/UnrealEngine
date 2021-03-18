// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EventManager.h"

namespace Chaos
{
	class CHAOS_API FEventDefaults
	{
	public:

		/**
		 * Register default event types
		 */
		static void RegisterSystemEvents(FEventManager& EventManager);

	private:

		/**
		 * Register collision event gathering function & data type
		 */
		static void RegisterCollisionEvent(FEventManager& EventManager);

		/**
		 * Register breaking event gathering function & data type
		 */
		static void RegisterBreakingEvent(FEventManager& EventManager);

		/**
		 * Register trailing event gathering function & data type
		 */
		static void RegisterTrailingEvent(FEventManager& EventManager);


		static void RegisterSleepingEvent(FEventManager& EventManager);

	};
}