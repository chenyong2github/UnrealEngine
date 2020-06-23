// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolSettings.h"
#include "Interfaces/IDMXProtocol.h"

UDMXProtocolSettings::UDMXProtocolSettings()
	: InterfaceIPAddress(TEXT("0.0.0.0"))
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

	// Default Attributes
	Attributes =
	{
		// Label					Keywords
		{ TEXT("Red"),				TEXT("red") },
		{ TEXT("Green"),			TEXT("green") },
		{ TEXT("Blue"),				TEXT("blue") },
		{ TEXT("Brightess"),		TEXT("brightness luminosity intensity strength") }
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

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, InterfaceIPAddress))
	{
		IDMXProtocol::OnNetworkInterfaceChanged.Broadcast(InterfaceIPAddress);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, FixtureCategories))
	{
		if (FixtureCategories.Num() == 0)
		{
			FixtureCategories.Add(TEXT("Other"));
		}

		FDMXFixtureCategory::OnValuesChanged.Broadcast();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, Attributes))
	{
		if (Attributes.Num() == 0)
		{
			Attributes.Add({ NAME_None, TEXT("") });
		}

		FDMXAttributeName::OnValuesChanged.Broadcast();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif // WITH_EDITOR