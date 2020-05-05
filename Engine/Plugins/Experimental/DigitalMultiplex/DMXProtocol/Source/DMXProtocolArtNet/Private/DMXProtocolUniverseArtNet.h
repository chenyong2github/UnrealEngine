// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IDMXProtocolUniverse.h"

struct FRDMUID;

class DMXPROTOCOLARTNET_API FDMXProtocolUniverseArtNet
	: public IDMXProtocolUniverse
{
public:
	FDMXProtocolUniverseArtNet(IDMXProtocolPtr InDMXProtocol, const FJsonObject& InSettings);

	//~ Begin IDMXProtocolDevice implementation
	virtual IDMXProtocolPtr GetProtocol() const override;
	virtual FDMXBufferPtr GetInputDMXBuffer() const override;
	virtual FDMXBufferPtr GetOutputDMXBuffer() const override;
	virtual bool SetDMXFragment(const IDMXFragmentMap& DMXFragment) override;
	virtual uint8 GetPriority() const override;
	virtual uint32 GetUniverseID() const override;
	virtual TSharedPtr<FJsonObject> GetSettings() const override;
	virtual bool IsSupportRDM() const override;
	//~ End IDMXProtocolDevice implementation

public:
	/**
	 * Represent physical ArtNet controller port ID. Values from 0 to 3
	 * @return The 8-bit port address
	 */
	uint8 GetPortID();

	/**
	 * The 8-bit port address, which is made up of the sub-net and universe
	 * @return The 8-bit port address
	 */
	uint8 GetPortAddress() const;
	//~ End IDMXProtocolPort implementation

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

	//~ ArtNet specific implemntation
	bool SetNetAddress(uint8 InNet);
	bool SetSubnetAddress(uint8 InSubnetAddress);
	bool SetUniverse(uint8 InUniverse);

private:
	IDMXProtocolPtrWeak WeakDMXProtocol;
	FDMXBufferPtr OutputDMXBuffer;
	FDMXBufferPtr InputDMXBuffer;
	uint8 Priority;
	uint16 UniverseID;

	uint8 PortID;
	uint8 PortAddress;
	uint8 NetAddress;
	uint8 SubnetAddress;
	uint8 UniverseAddress;
	bool bIsRDMSupport;
	TSharedPtr<FJsonObject> Settings;

	// Array of TODs UID. This is using for RDM discovery
	TArray<FRDMUID> TODUIDs;
};