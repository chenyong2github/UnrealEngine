// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IDMXProtocolDevice.h"
#include "Dom/JsonObject.h"

class DMXPROTOCOLARTNET_API FDMXProtocolDeviceArtNet
	: public IDMXProtocolDevice
{
public:
	FDMXProtocolDeviceArtNet(IDMXProtocol* InDMXProtocol, TSharedPtr<IDMXProtocolInterface> InProtocolInterface, FJsonObject& InSettings, uint32 InDeviceID);

	//~ Begin IDMXProtocolDevice implementation
	virtual TSharedPtr<FJsonObject> GetSettings() const override;
	virtual TWeakPtr<IDMXProtocolInterface> GetCachedProtocolInterface() const override;
	virtual IDMXProtocol* GetProtocol() const override;
	virtual uint32 GetDeviceID() const override;

	virtual bool AllowLooping() const override;
	//~ End IDMXProtocolDevice implementation

private:
	IDMXProtocol* DMXProtocol;
	TWeakPtr<IDMXProtocolInterface> ProtocolInterface;
	TSharedPtr<FJsonObject> Settings;
	uint32 DeviceID;
};