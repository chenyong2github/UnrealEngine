// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocol.h"

#include "Managers/DMXProtocolDeviceManager.h"
#include "Managers/DMXProtocolInterfaceManager.h"
#include "Managers/DMXProtocolPortManager.h"
#include "Managers/DMXProtocolUniverseManager.h"

#include "Interfaces/IDMXProtocolPort.h"
#include "Interfaces/IDMXProtocolUniverse.h"

#include "DMXProtocolTypes.h"
#include "Dom/JsonObject.h"

#include "HAL/IConsoleManager.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY(LogDMXProtocol);

FDMXProtocol::FDMXProtocol(const FName& InProtocolName, FJsonObject& InSettings)
{
	Settings = MakeShared<FJsonObject>(InSettings);
	InterfaceManager = MakeShared<FDMXProtocolInterfaceManager>(this);
	DeviceManager = MakeShared<FDMXProtocolDeviceManager>(this);
	PortManager = MakeShared<FDMXProtocolPortManager>(this);
	UniverseManager = MakeShared<FDMXProtocolUniverseManager>(this);
}

FDMXProtocol::~FDMXProtocol()
{
	InterfaceManager->RemoveAll();
	DeviceManager->RemoveAll();
	PortManager->RemoveAll();
	UniverseManager->RemoveAll();
}

TSharedPtr<FDMXProtocolDeviceManager> FDMXProtocol::GetDeviceManager() const
{
	return DeviceManager;
}

TSharedPtr<FDMXProtocolInterfaceManager> FDMXProtocol::GetInterfaceManager() const
{
	return InterfaceManager;
}

TSharedPtr<FDMXProtocolPortManager> FDMXProtocol::GetPortManager() const
{
	return PortManager;
}

TSharedPtr<FDMXProtocolUniverseManager> FDMXProtocol::GetUniverseManager() const
{
	return UniverseManager;
}

TSharedPtr<FJsonObject> FDMXProtocol::GetSettings() const
{
	return Settings;
}

void FDMXProtocol::SetDMXFragment(uint16 UniverseID, const IDMXFragmentMap& DMXFragment, bool bShouldSend)
{
	IDMXProtocolUniverse* Universe = UniverseManager->GetUniverseById(UniverseID);
	if (Universe != nullptr)
	{
		// Update Universe DMX buffer
		Universe->SetDMXFragment(DMXFragment);
		if (bShouldSend)
		{
			TSharedPtr<IDMXProtocolPort> Port = Universe->GetCachedUniversePort().Pin();
			if (Port.IsValid())
			{
				Port->WriteDMX(Universe->GetOutputDMXBuffer());
			}
		}
	}
}
