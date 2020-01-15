// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DMXProtocolSettings.generated.h"

/**
 * User defined protocol settings that apply to a whole protocol module
 */
UCLASS(config = Engine)
class DMXPROTOCOL_API UDMXProtocolSettings : public UObject
{
public:
	GENERATED_BODY()

public:
	UDMXProtocolSettings() :
		InterfaceIPAddress(TEXT("0.0.0.0"))
	{}


public:
	/** Manual Interface IP Address */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "DMX", Meta = (DisplayName = "Interface IP address"))
	FString InterfaceIPAddress;
};
