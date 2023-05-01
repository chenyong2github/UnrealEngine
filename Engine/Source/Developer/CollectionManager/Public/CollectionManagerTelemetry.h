// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <variant>

#include "AnalyticsTimedTelemetryEvents.h"
#include "Delegates/Delegate.h"

/**
 * Events that the CollectionManager emits for telemetry tracking purposes
 */
namespace UE::CollectionManager::Analytics
{
	/**
	 * StatusCode bitfield positions
	 */
	struct StatusCodeFields
	{
		// Operation uses Source control
		static constexpr uint64 UsesSCC = 1llu << 62;
		// Operation was successful
		static constexpr uint64 Success = 1llu << 63;
	};
	struct FAddObjects : UE::Analytics::FTimedTelemetryEvent {};
	struct FRemoveObjects : UE::Analytics::FTimedTelemetryEvent {};
	
	using FEvent = std::variant<FAddObjects, FRemoveObjects>;

	DECLARE_DELEGATE_OneParam(FEmitAnalyticsEvent, const FEvent&)
}