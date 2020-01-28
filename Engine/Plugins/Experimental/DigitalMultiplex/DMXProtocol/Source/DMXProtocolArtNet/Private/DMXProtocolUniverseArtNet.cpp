// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolUniverseArtNet.h"
#include "Dom/JsonObject.h"

#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolPort.h"
#include "Interfaces/IDMXProtocolDevice.h"
#include "DMXProtocolConstants.h"
#include "DMXProtocolTypes.h"

FDMXProtocolUniverseArtNet::FDMXProtocolUniverseArtNet(IDMXProtocol* InDMXProtocol, TSharedPtr<IDMXProtocolPort> InPort)
	: DMXProtocol(InDMXProtocol)
	, Port(InPort)
	, Priority(0)
	, UniverseID(InPort->GetUniverseID())
{
	checkf(DMXProtocol, TEXT("DMXProtocol pointer is nullptr"));
	checkf(Port.IsValid(), TEXT("DMXProtocol port is not valid"));

	InputDMXBuffer = MakeShared<FDMXBuffer>();
	OutputDMXBuffer = MakeShared<FDMXBuffer>();
}


IDMXProtocol * FDMXProtocolUniverseArtNet::GetProtocol() const
{
	return DMXProtocol;
}

TSharedPtr<FDMXBuffer> FDMXProtocolUniverseArtNet::GetOutputDMXBuffer() const
{
	return OutputDMXBuffer;
}

TSharedPtr<FDMXBuffer> FDMXProtocolUniverseArtNet::GetInputDMXBuffer() const
{
	return InputDMXBuffer;
}

bool FDMXProtocolUniverseArtNet::SetDMXFragment(const IDMXFragmentMap & DMXFragment)
{
	OutputDMXBuffer->SetDMXFragment(DMXFragment);
	return false;
}

uint8 FDMXProtocolUniverseArtNet::GetPriority() const
{
	return Priority;
}

uint16 FDMXProtocolUniverseArtNet::GetUniverseID() const
{
	return UniverseID;
}

TWeakPtr<IDMXProtocolPort> FDMXProtocolUniverseArtNet::GetCachedUniversePort() const
{
	return Port;
}
