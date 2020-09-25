// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Devices/DisplayClusterInputDeviceTraits.h"
#include "IDisplayClusterStringSerializable.h"

#include "DisplayClusterConfigurationTypes.h"


/**
 * Interface for input devices
 */
class IDisplayClusterInputDevice
	: public IDisplayClusterStringSerializable
{
public:
	virtual ~IDisplayClusterInputDevice() = 0
	{ }

	virtual FString GetId() const = 0;
	virtual FString GetType() const = 0;
	virtual EDisplayClusterInputDeviceType    GetTypeId() const = 0;

	virtual bool Initialize() = 0;
	virtual void PreUpdate() = 0;
	virtual void Update() = 0;
	virtual void PostUpdate() = 0;

	virtual FString ToString() const = 0;
};

