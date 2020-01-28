// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPtr.h"

#include "DataSourceFilter.h"

#include "TraceSourceFilteringProjectSettings.generated.h"

UCLASS(config = Engine, meta = (DisplayName = "Trace Source Filtering"), defaultconfig)
class UTraceSourceFilteringProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, Category = TraceSourceFiltering, AdvancedDisplay, meta = (DisplayName = "Source Filter Classes, which should be incorporated into the cook", RelativeToGameContentDir))
	TArray<TSoftClassPtr<UDataSourceFilter>> CookedSourceFilterClasses;
};
