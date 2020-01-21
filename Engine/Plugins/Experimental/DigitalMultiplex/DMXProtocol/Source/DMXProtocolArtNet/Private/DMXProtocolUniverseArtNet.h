// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IDMXProtocolUniverse.h"

class DMXPROTOCOLARTNET_API FDMXProtocolUniverseArtNet
	: public IDMXProtocolUniverse
{
public:
	FDMXProtocolUniverseArtNet(IDMXProtocol* InDMXProtocol, TSharedPtr<IDMXProtocolPort> InPort);

	//~ Begin IDMXProtocolDevice implementation
	virtual IDMXProtocol* GetProtocol() const override;
	virtual TSharedPtr<FDMXBuffer> GetOutputDMXBuffer() const override;
	virtual TSharedPtr<FDMXBuffer> GetInputDMXBuffer() const override;
	virtual bool SetDMXFragment(const IDMXFragmentMap& DMXFragment) override;
	virtual uint8 GetPriority() const override;
	virtual uint16 GetUniverseID() const override;
	virtual TWeakPtr<IDMXProtocolPort> GetCachedUniversePort() const override;
	//~ End IDMXProtocolDevice implementation

private:
	IDMXProtocol* DMXProtocol;
	TWeakPtr<IDMXProtocolPort> Port;
	TSharedPtr<FDMXBuffer> OutputDMXBuffer;
	TSharedPtr<FDMXBuffer> InputDMXBuffer;
	uint8 Priority;
	uint16 UniverseID;
};