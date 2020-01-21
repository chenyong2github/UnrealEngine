// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IDMXProtocolPort.h"

class IDMXProtocol;

class DMXPROTOCOLSACN_API FDMXProtocolPortSACN
	: public IDMXProtocolPort
{
public:
	FDMXProtocolPortSACN(IDMXProtocol* InDMXProtocol, TSharedPtr<IDMXProtocolDevice> InProtocolDevice, uint8 InPortID, uint16 InUniverseID, EDMXPortDirection InPortDirection);

	//~ Begin IDMXProtocolPort implementation
	virtual IDMXProtocol* GetProtocol() const override;

	virtual uint8 GetPortID() const override;
	virtual uint8 GetPriority() const override;
	virtual void SetPriotiy(uint8 InPriority) override;
	virtual bool IsSupportRDM() const override;
	virtual TWeakPtr<IDMXProtocolDevice> GetCachedDevice() const override;
	virtual TWeakPtr<IDMXProtocolUniverse> GetCachedUniverse() const override;
	virtual void SetUniverse(const TSharedPtr<IDMXProtocolUniverse>& InUniverse) override;
	virtual EDMXPortCapability GetPortCapability() const override;
	virtual EDMXPortDirection GetPortDirection() const override;
	virtual bool WriteDMX(const TSharedPtr<FDMXBuffer>& DMXBuffer) override;
	virtual bool ReadDMX() override;
	virtual TSharedPtr<FJsonObject> GetSettings() const override;
	virtual uint16 GetUniverseID() const override;
	//~ End IDMXProtocolPort implementation

private:
	IDMXProtocol* DMXProtocol;
	TWeakPtr<IDMXProtocolDevice> ProtocolDevice;
	TWeakPtr<IDMXProtocolUniverse> ProtocolUniverse;
	uint8 PortID;
	uint16 UniverseID;
	uint8 Priority;
	bool bIsRDMSupport;
	EDMXPortDirection PortDirection;
	TSharedPtr<FJsonObject> Settings;
};
