// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IDMXProtocolInterface.h"

class DMXPROTOCOLARTNET_API FDMXProtocolInterfaceArtNet
	: public IDMXProtocolInterfaceEthernet
{
public:
	FDMXProtocolInterfaceArtNet(IDMXProtocol* InDMXProtocol);

	//~ Begin IDMXProtocolInterface implementation
	virtual IDMXProtocol* GetProtocol() const override;
	//~ End IDMXProtocolInterface implementation

private:
	IDMXProtocol* DMXProtocol;
};