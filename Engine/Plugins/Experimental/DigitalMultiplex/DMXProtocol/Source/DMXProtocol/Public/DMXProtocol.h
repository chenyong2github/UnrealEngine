// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"
#include "Interfaces/IDMXProtocol.h"

class DMXPROTOCOL_API FDMXProtocol 
	: public IDMXProtocol
{
public:

	//~ Begin IDMXProtocol implementation
	virtual TSharedPtr<FDMXProtocolDeviceManager> GetDeviceManager() const override;
	
	virtual TSharedPtr<FDMXProtocolInterfaceManager> GetInterfaceManager() const override;

	virtual TSharedPtr<FDMXProtocolPortManager> GetPortManager() const override;
	
	virtual TSharedPtr<FDMXProtocolUniverseManager> GetUniverseManager() const override;

	virtual TSharedPtr<FJsonObject> GetSettings() const override;

	virtual void SetDMXFragment(uint16 UniverseID, const IDMXFragmentMap& DMXFragment, bool bShouldSend = true) override;
	//~ End IDMXProtocol implementation

protected:
	FDMXProtocol() = delete;
	FDMXProtocol(const FName& InProtocolName, FJsonObject& InSettings);
	~FDMXProtocol();

protected:
	TSharedPtr<FDMXProtocolDeviceManager> DeviceManager;
	TSharedPtr<FDMXProtocolInterfaceManager> InterfaceManager;
	TSharedPtr<FDMXProtocolPortManager> PortManager;
	TSharedPtr<FDMXProtocolUniverseManager> UniverseManager;
	TSharedPtr<FJsonObject> Settings;
};