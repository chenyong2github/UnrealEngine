// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"
#include "Interfaces/IDMXProtocol.h"

class DMXPROTOCOL_API FDMXProtocolUniverseManager
{
public:
	FDMXProtocolUniverseManager(IDMXProtocol* InDMXProtocol)
		: DMXProtocol(InDMXProtocol)
	{}

public:
	void AddUniverse(TSharedPtr<IDMXProtocolPort> InPort, TSharedPtr<IDMXProtocolUniverse>  InUniverse);
	void RemoveUniverse(TSharedPtr<IDMXProtocolPort> InPort, TSharedPtr<IDMXProtocolUniverse>  InUniverse);
	void RemoveAll();
	IDMXProtocolUniverse* GetUniverseByPort(IDMXProtocolPort* InPort);
	IDMXProtocolUniverse* GetUniverseById(uint16 UniverseID);

private:
	IUniversesMap UniversesMap;
	IUniversesIDMap UniversesIDMap;
	IDMXProtocol* DMXProtocol;
};
