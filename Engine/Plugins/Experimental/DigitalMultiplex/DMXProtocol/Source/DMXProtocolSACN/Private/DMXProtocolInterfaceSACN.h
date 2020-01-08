// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IDMXProtocolInterface.h"

class DMXPROTOCOLSACN_API FDMXProtocolInterfaceSACN
	: public IDMXProtocolInterfaceEthernet
{
public:
	FDMXProtocolInterfaceSACN(IDMXProtocol* InDMXProtocol);

	//~ Begin IDMXProtocolInterface implementation
	virtual IDMXProtocol* GetProtocol() const override;
	//~ End IDMXProtocolInterface implementation

private:
	IDMXProtocol* DMXProtocol;
};