// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EventManager.h"

namespace Chaos
{
	template <typename Traits>
	class TEventDefaults
	{
	public:

		/**
		 * Register default event types
		 */
		static void RegisterSystemEvents(TEventManager<Traits>& EventManager);

	private:

		/**
		 * Register collision event gathering function & data type
		 */
		static void RegisterCollisionEvent(TEventManager<Traits>& EventManager);

		/**
		 * Register breaking event gathering function & data type
		 */
		static void RegisterBreakingEvent(TEventManager<Traits>& EventManager);

		/**
		 * Register trailing event gathering function & data type
		 */
		static void RegisterTrailingEvent(TEventManager<Traits>& EventManager);


		static void RegisterSleepingEvent(TEventManager<Traits>& EventManager);

	};

#define EVOLUTION_TRAIT(Trait) extern template class CHAOS_TEMPLATE_API TEventDefaults<Trait>;
#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT
}