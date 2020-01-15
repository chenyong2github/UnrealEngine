// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IDMXProtocolPort.h"
#include "DMXProtocolTypes.h"

class IDMXProtocol;

class DMXPROTOCOLARTNET_API FDMXProtocolPortArtNet
	: public IDMXProtocolPort
{
public:
	FDMXProtocolPortArtNet(IDMXProtocol* InDMXProtocol, TSharedPtr<IDMXProtocolDevice> InProtocolDevice, const FJsonObject& InSettings, uint8 InPortID, EDMXPortDirection InPortDirection);

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

	/**
	 * The 8-bit port address, which is made up of the sub-net and universe
	 * @return The 8-bit port address
	 */
	virtual uint8 GetPortAddress() const override;
	//~ End IDMXProtocolPort implementation

private:
	//~ ArtNet specific implemntation
	bool SetNetAddress(uint8 InNet);
	bool SetSubnetAddress(uint8 InSubnetAddress);
	bool SetUniverse(uint8 InUniverse);

public:
	/**
	 * Bits 14-8 
	 * @return The 8-bit net address
	 */
	uint8 GetNetAddress() const;

	/**
	 * Bits 7-4
	 * @return The 8-bit subnet address
	 */
	uint8 GetSubnetAddress() const;

	/**
	 * Bits 3-0
	 * @return The 8-bit uni address
	 */
	uint8 GetUniverseAddress() const;

	const TArray<FRDMUID>& GetTODUIDs() const;

	void AddTODUID(const FRDMUID& InUID);

private:
	IDMXProtocol* DMXProtocol;
	TWeakPtr<IDMXProtocolDevice> ProtocolDevice;
	TWeakPtr<IDMXProtocolUniverse> ProtocolUniverse;
	uint8 PortID;
	uint8 PortAddress;
	uint8 NetAddress;
	uint8 SubnetAddress;
	uint8 UniverseAddress;
	uint8 Priority;
	uint16 UniverseID;
	bool bIsRDMSupport;
	EDMXPortDirection PortDirection;
	TSharedPtr<FJsonObject> Settings;

	// Array of TODs UID. This is using for RDM discovery
	TArray<FRDMUID> TODUIDs;
};
