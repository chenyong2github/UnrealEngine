// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IRemoteControlInterceptionFeature.h"


/**
 * Remote control interception processor feature
 */
class FRemoteControlInterceptionProcessor :
	public IRemoteControlInterceptionFeatureProcessor
{
public:
	FRemoteControlInterceptionProcessor() = default;
	virtual ~FRemoteControlInterceptionProcessor() = default;

public:
	// IRemoteControlInterceptionCommands interface
	virtual void SetObjectProperties(FRCIPropertiesMetadata& InProperties) override;
	virtual void ResetObjectProperties(FRCIObjectMetadata& InObject) override;
	virtual void InvokeCall(FRCIFunctionMetadata& InFunction) override;
	// ~IRemoteControlInterceptionCommands interface
};
