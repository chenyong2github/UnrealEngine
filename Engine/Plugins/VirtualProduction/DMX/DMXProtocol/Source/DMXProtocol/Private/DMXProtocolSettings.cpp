// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolSettings.h"
#include "Interfaces/IDMXProtocol.h"
#include "DMXProtocolConstants.h"
UDMXProtocolSettings::UDMXProtocolSettings()
	: InterfaceIPAddress(TEXT("0.0.0.0"))
	, SendingRefreshRate(DMX_MAX_REFRESH_RATE)
	, bDefaultReceiveDMXEnabled(true)
	, ReceivingRefreshRate(DMX_MAX_REFRESH_RATE)
	, bUseSeparateReceivingThread(true)
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
		{ TEXT("Reset"),			TEXT("FixtureReset, FixtureGlobalReset, GlobalReset") }
	};
}

#if WITH_EDITOR

void UDMXProtocolSettings::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, InterfaceIPAddress))
	{
		IDMXProtocol::OnNetworkInterfaceChanged.Broadcast(InterfaceIPAddress);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, bUseSeparateReceivingThread) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, ReceivingRefreshRate)
		)
	{
		IDMXProtocol::OnReceivingThreadChanged.Broadcast(ReceivingRefreshRate, bUseSeparateReceivingThread);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, FixtureCategories))
	{
		if (FixtureCategories.Num() == 0)
		{
			FixtureCategories.Add(TEXT("Other"));
		}

		FDMXFixtureCategory::OnValuesChanged.Broadcast();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, Attributes)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FDMXAttribute, Name)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FDMXAttribute, Keywords))
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
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, bDefaultReceiveDMXEnabled))
	{
		GlobalSetReceiveDMXEnabled(bDefaultReceiveDMXEnabled);
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
}

void UDMXProtocolSettings::GlobalSetReceiveDMXEnabled(bool bReceiveDMXEnabled)
{
	TMap<FName, IDMXProtocolPtr> AllProtocols = FDMXProtocolModule::Get().GetProtocols();

	for (const TPair<FName, IDMXProtocolPtr>& ProtocolKvp : AllProtocols)
	{
		IDMXProtocolPtr Protocol = ProtocolKvp.Value;
		check(Protocol.IsValid());

		ProtocolKvp.Value->SetReceiveDMXEnabled(bReceiveDMXEnabled);
	}
}
