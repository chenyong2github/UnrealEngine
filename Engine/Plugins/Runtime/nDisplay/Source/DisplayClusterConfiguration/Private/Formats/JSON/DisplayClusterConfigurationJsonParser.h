// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Formats/IDisplayClusterConfigurationDataParser.h"
#include "Formats/JSON/DisplayClusterConfigurationJsonTypes.h"

class UDisplayClusterConfigurationData;


/**
 * Config parser for JSON based config files
 */
class FDisplayClusterConfigurationJsonParser
	: public IDisplayClusterConfigurationDataParser
{
public:
	FDisplayClusterConfigurationJsonParser()  = default;
	~FDisplayClusterConfigurationJsonParser() = default;

public:
	// Load data from a specified file
	virtual UDisplayClusterConfigurationData* LoadData(const FString& FilePath, UObject* Owner = nullptr) override;
	// Save data to a specified file
	virtual bool SaveData(const UDisplayClusterConfigurationData* ConfigData, const FString& FilePath) override;

protected:
	// [import data] Fill generic data container with parsed information
	UDisplayClusterConfigurationData* ConvertDataToInternalTypes();
	// [export data] Extract data from generic container to specific containers
	bool ConvertDataToExternalTypes(const UDisplayClusterConfigurationData* InData);

private:
	// Intermediate temporary data (Json types)
	FDisplayClusterConfigurationJsonContainer JsonData;
	// Source file
	FString ConfigFile;
	// Owner for config data UObject we'll create on success
	UObject* ConfigDataOwner = nullptr;
};
