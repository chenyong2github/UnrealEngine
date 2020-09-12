// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXRuntimeBlueprintLibrary.h"

#include "Interfaces/IDMXProtocol.h"


void UDMXRuntimeBlueprintLibrary::SetReceiveDMXEnabled(bool bReceiveDMXEnabled, bool bAffectEditor)
{
	if (bAffectEditor || !GIsEditor)
	{
		TMap<FName, IDMXProtocolPtr> AllProtocols = FDMXProtocolModule::Get().GetProtocols();

		for (const TPair<FName, IDMXProtocolPtr>& ProtocolKvp : AllProtocols)
		{
			IDMXProtocolPtr Protocol = ProtocolKvp.Value;
			check(Protocol.IsValid());

			Protocol->SetReceiveDMXEnabled(bReceiveDMXEnabled);
		}
	}
}

bool UDMXRuntimeBlueprintLibrary::IsReceiveDMXEnabled()
{
	TMap<FName, IDMXProtocolPtr> AllProtocols = FDMXProtocolModule::Get().GetProtocols();

	for (const TPair<FName, IDMXProtocolPtr>& ProtocolKvp : AllProtocols)
	{
		IDMXProtocolPtr Protocol = ProtocolKvp.Value;
		check(Protocol.IsValid());

		if (Protocol->IsReceiveDMXEnabled())
		{
			return true;
		}
	}

	return false;
}
