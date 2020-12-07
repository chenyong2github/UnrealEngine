// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataRegistryTypes.h"
#include "Engine/DeveloperSettings.h"
#include "DataRegistrySettings.generated.h"


/** Settings for data registry, modifies where to load registry assets and how to access */
UCLASS(config = Game, defaultconfig, notplaceable, meta = (DisplayName = "Data Registry"))
class DATAREGISTRY_API UDataRegistrySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	
	/** List of directories to scan for data registry assets */
	UPROPERTY(config, EditAnywhere, Category = "Data Registry", meta = (RelativeToGameContentDir, LongPackageName))
	TArray<FDirectoryPath> DirectoriesToScan;

	/** If false, only registry assets inside DirectoriesToScan will be initialized. If True, it will also initialize any in-memory DataRegistry assets outside the scan paths */
	UPROPERTY(config, EditAnywhere, Category = "Data Registry")
	bool bInitializeAllLoadedRegistries = false;

	/** If true, cooked builds will ignore errors with missing asset register data for manually registered specific assets as it may have been stripped out */
	UPROPERTY(config, EditAnywhere, Category = "Data Registry")
	bool bIgnoreMissingCookedAssetRegistryData = false;


#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

};