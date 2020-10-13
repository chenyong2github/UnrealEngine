// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolBlueprintLibrary.h"

#include "DMXProtocolSettings.h"

#include "Interfaces/IDMXProtocol.h"

void UDMXProtocolBlueprintLibrary::SetSendDMXEnabled(bool bSendDMXEnabled, bool bAffectEditor)
{
	if (bAffectEditor || !GIsEditor)
	{
		UDMXProtocolSettings* ProtocolSettings = CastChecked<UDMXProtocolSettings>(UDMXProtocolSettings::StaticClass()->GetDefaultObject());
		ProtocolSettings->OverrideSendDMXEnabled(bSendDMXEnabled);		

		TMap<FName, IDMXProtocolPtr> AllProtocols = FDMXProtocolModule::Get().GetProtocols();

		for (const TPair<FName, IDMXProtocolPtr>& ProtocolKvp : AllProtocols)
		{
			IDMXProtocolPtr Protocol = ProtocolKvp.Value;
			check(Protocol.IsValid());

			Protocol->SetSendDMXEnabled(bSendDMXEnabled);
		}
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
		UDMXProtocolSettings* ProtocolSettings = CastChecked<UDMXProtocolSettings>(UDMXProtocolSettings::StaticClass()->GetDefaultObject());
		ProtocolSettings->OverrideReceiveDMXEnabled(bReceiveDMXEnabled);

		TMap<FName, IDMXProtocolPtr> AllProtocols = FDMXProtocolModule::Get().GetProtocols();

		for (const TPair<FName, IDMXProtocolPtr>& ProtocolKvp : AllProtocols)
		{
			IDMXProtocolPtr Protocol = ProtocolKvp.Value;
			check(Protocol.IsValid());

			Protocol->SetReceiveDMXEnabled(bReceiveDMXEnabled);
		}
	}
}

bool UDMXProtocolBlueprintLibrary::IsReceiveDMXEnabled()
{
	const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();
	return ProtocolSettings->IsReceiveDMXEnabled();
}
