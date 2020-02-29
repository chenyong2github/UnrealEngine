// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolSettings.h"
#include "Interfaces/IDMXProtocol.h"

UDMXProtocolSettings::UDMXProtocolSettings()
	: InterfaceIPAddress(TEXT("0.0.0.0"))
	, bShouldUseUnicast(false)
{
	FixtureCategories =
	{
		TEXT("Static"),
		TEXT("Matrix/Pixel Bar"),
		TEXT("Moving Head"),
		TEXT("Moving Mirror"),
		TEXT("Strobe"),
		TEXT("Other")
	};
}

#if WITH_EDITOR

void UDMXProtocolSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FName PropertyName;
	if (PropertyChangedEvent.Property)
	{
		PropertyName = PropertyChangedEvent.Property->GetFName();
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, InterfaceIPAddress) || PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, UnicastEndpoint))
	{
		IDMXProtocol::OnNetworkInterfaceChanged.Broadcast(InterfaceIPAddress);
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, FixtureCategories))
	{
		if (FixtureCategories.Num() == 0)
		{
			FixtureCategories.Add(TEXT("Other"));
		}

		FDMXFixtureCategory::OnPossibleValuesUpdated.Broadcast();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif // WITH_EDITOR