// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolBlueprintLibrary.h"

#include "DMXProtocolSettings.h"

#include "Interfaces/IDMXProtocol.h"


void UDMXProtocolBlueprintLibrary::SetSendDMXEnabled(bool bSendDMXEnabled, bool bAffectEditor)
{
	if (bAffectEditor || !GIsEditor)
	{
		UDMXProtocolSettings* ProtocolSettings = GetMutableDefault<UDMXProtocolSettings>();
		ProtocolSettings->OverrideSendDMXEnabled(bSendDMXEnabled);		
	}
}

bool UDMXProtocolBlueprintLibrary::IsSendDMXEnabled()
{
	const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();
	return ProtocolSettings->IsSendDMXEnabled();
}

void UDMXProtocolBlueprintLibrary::SetReceiveDMXEnabled(bool bReceiveDMXEnabled, bool bAffectEditor)
{
	if (bAffectEditor || !GIsEditor)
	{
		UDMXProtocolSettings* ProtocolSettings = GetMutableDefault<UDMXProtocolSettings>();
		ProtocolSettings->OverrideReceiveDMXEnabled(bReceiveDMXEnabled);
	}
}

bool UDMXProtocolBlueprintLibrary::IsReceiveDMXEnabled()
{
	const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();
	return ProtocolSettings->IsReceiveDMXEnabled();
}
