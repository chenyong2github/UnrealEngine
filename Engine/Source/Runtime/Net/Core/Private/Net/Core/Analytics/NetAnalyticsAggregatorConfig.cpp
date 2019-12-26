// Copyright Epic Games, Inc. All Rights Reserved.

#include "Net/Core/Analytics/NetAnalyticsAggregatorConfig.h"


/**
 * UNetAnalyticsAggregatorConfig
 */
UNetAnalyticsAggregatorConfig::UNetAnalyticsAggregatorConfig(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UNetAnalyticsAggregatorConfig::OverridePerObjectConfigSection(FString& SectionName)
{
	SectionName = GetName() + TEXT(" ") + GetClass()->GetName();
}
