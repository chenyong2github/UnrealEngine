// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SmartObjectTypes.h"
#include "Engine/DeveloperSettings.h"
#include "SmartObjectSettings.generated.h"

UCLASS(config = SmartObjects, defaultconfig, DisplayName = "SmartObject", AutoExpandCategories = "SmartObject")
class SMARTOBJECTSMODULE_API USmartObjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * Default TagFiltering policy to use for newly created SmartObjectDefinitions.
	 * Indicates how Tags and TagQueries (User and Activity) from slots and parent object will be processed for find requests.
	 * Tag Queries (ObjectTagFilter) from definitions tested against SmartObject instances tags are not affected.
	 */
	UPROPERTY(EditAnywhere, config, Category = SmartObject)
	ESmartObjectTagFilteringPolicy DefaultTagFilteringPolicy = ESmartObjectTagFilteringPolicy::Override;
};
