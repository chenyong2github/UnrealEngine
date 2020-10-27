// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolSettings.h"

#include "DMXProtocolBlueprintLibrary.h"
#include "DMXProtocolConstants.h"
#include "Interfaces/IDMXProtocol.h"

#include "IPAddress.h"
#include "SocketSubsystem.h"


UDMXProtocolSettings::UDMXProtocolSettings()
	: SendingRefreshRate(DMX_MAX_REFRESH_RATE)
	, ReceivingRefreshRate(DMX_MAX_REFRESH_RATE)
	, bDefaultReceiveDMXEnabled(true)
	, bDefaultSendDMXEnabled(true)
	, bOverrideReceiveDMXEnabled(true)
	, bOverrideSendDMXEnabled(true)	
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
		/* Fixture related */

		// Label					Keywords
		{ TEXT("Color"),			TEXT("ColorWheel, Color1") },
		{ TEXT("Red"),				TEXT("ColorAdd_R") },
		{ TEXT("Green"),			TEXT("ColorAdd_G") },
		{ TEXT("Blue"),				TEXT("ColorAdd_B") },
		{ TEXT("Cyan"),				TEXT("ColorAdd_C, ColorSub_C") },
		{ TEXT("Magenta"),			TEXT("ColorAdd_M, ColorSub_M") },
		{ TEXT("Yellow"),			TEXT("ColorAdd_Y, ColorSub_Y") },
		{ TEXT("White"),			TEXT("ColorAdd_W") },
		{ TEXT("Amber"),			TEXT("ColorAdd_A") },
		{ TEXT("Dimmer"),			TEXT("Intensity, Strength, Brightness") },
		{ TEXT("Pan"),				TEXT("") },
		{ TEXT("Shutter"),			TEXT("Strobe") },
		{ TEXT("Tilt"),				TEXT("") },
		{ TEXT("Zoom"),				TEXT("") },
		{ TEXT("Focus"),			TEXT("") },
		{ TEXT("Iris"),				TEXT("") },
		{ TEXT("Gobo"),				TEXT("GoboWheel, Gobo1") },
		{ TEXT("Gobo Spin"),		TEXT("GoboSpin") },
		{ TEXT("Gobo Wheel Rotate"),TEXT("GoboWheelSpin, GoboWheelRotate") },
		{ TEXT("Color Rotation"),	TEXT("ColorWheelSpin") },
		{ TEXT("Shaper Rotation"),	TEXT("ShaperRot") },
		{ TEXT("Effects"),			TEXT("Effect, Macro, Effects") },
		{ TEXT("Frost"),			TEXT("") },
		{ TEXT("Reset"),			TEXT("FixtureReset, FixtureGlobalReset, GlobalReset") },


		/* Firework, Fountain related */

		// Label					Keywords
		{ TEXT("ModeStartStop"),	TEXT("") },
		{ TEXT("Burst"),			TEXT("") },
		{ TEXT("Launch"),			TEXT("") },
		{ TEXT("Velocity"),			TEXT("") },
		{ TEXT("Angle"),			TEXT("") },
		{ TEXT("NumBeams"),			TEXT("") }
	};


	// Find a good default default network interface IP address
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

	TSharedRef<FInternetAddr> PreferedIPAddress = SocketSubsystem->CreateInternetAddr();
	PreferedIPAddress->SetIp(*TEXT("127.0.0.1"));

	TArray<TSharedPtr<FInternetAddr>> Addresses;
	SocketSubsystem->GetLocalAdapterAddresses(Addresses);

	if (Addresses.Contains(PreferedIPAddress))
	{
		InterfaceIPAddress = TEXT("127.0.0.1");
	}
	else
	{
		InterfaceIPAddress = TEXT("0.0.0.0");
	}
}

#if WITH_EDITOR
void UDMXProtocolSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, bDefaultReceiveDMXEnabled))
	{
		bOverrideReceiveDMXEnabled = bDefaultReceiveDMXEnabled;
		UDMXProtocolBlueprintLibrary::SetReceiveDMXEnabled(bDefaultReceiveDMXEnabled);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, bDefaultSendDMXEnabled))
	{
		bOverrideSendDMXEnabled = bDefaultSendDMXEnabled;
		UDMXProtocolBlueprintLibrary::SetSendDMXEnabled(bDefaultSendDMXEnabled);
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXProtocolSettings::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

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
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, Attributes) || 
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXAttribute, Name) || 
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXAttribute, Keywords))
	{
		if (Attributes.Num() == 0)
		{
			Attributes.Add({ NAME_None, TEXT("") });
		}

		for (FDMXAttribute& Attribute : Attributes)
		{
			Attribute.CleanupKeywords();
		}

		FDMXAttributeName::OnValuesChanged.Broadcast();
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR

void UDMXProtocolSettings::PostInitProperties()
{
	Super::PostInitProperties();

	// force cleanup of the keywords on load
	// this is required for supporting previous implementations
	// where spaces were used
	for (FDMXAttribute& Attribute : Attributes)
	{
		Attribute.CleanupKeywords();
	}

	bOverrideSendDMXEnabled = bDefaultSendDMXEnabled;
	bOverrideReceiveDMXEnabled = bDefaultReceiveDMXEnabled;
}
