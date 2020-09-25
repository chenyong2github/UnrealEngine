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
	virtual UDisplayClusterConfigurationData* LoadData(const FString& FilePath) override;
	// Save data to a specified file
	virtual bool SaveData(const UDisplayClusterConfigurationData* ConfigData, const FString& FilePath) override;

protected:
	// Fill generic data container with parsed information
	UDisplayClusterConfigurationData* ConvertDataToInternalTypes();
	// Fill generic data container with parsed information
	bool ConvertDataToExternalTypes(const UDisplayClusterConfigurationData* InData);

private:
	// Intermediate temporary data (Json types)
	FDisplayClusterConfigurationJsonContainer JsonData;
};
