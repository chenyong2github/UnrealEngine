// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolSettings.h"

#include "DMXProtocolBlueprintLibrary.h"
#include "DMXProtocolConstants.h"
#include "IO/DMXInputPort.h"
#include "IO/DMXOutputPort.h"
#include "Interfaces/IDMXProtocol.h"
#include "IO/DMXPortManager.h"


UDMXProtocolSettings::UDMXProtocolSettings()
	: SendingRefreshRate(DMX_RATE)
	, ReceivingRefreshRate(DMX_RATE)
	, bDefaultSendDMXEnabled(true)
	, bDefaultReceiveDMXEnabled(true)
	, bOverrideSendDMXEnabled(true)	
	, bOverrideReceiveDMXEnabled(true)
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
}

void UDMXProtocolSettings::PostInitProperties()
{
	Super::PostInitProperties();

	// Force cleanup of the keywords on load
	// This is required for supporting previous implementations where spaces were used
	for (FDMXAttribute& Attribute : Attributes)
	{
		Attribute.CleanupKeywords();
	}

	bOverrideSendDMXEnabled = bDefaultSendDMXEnabled;
	bOverrideReceiveDMXEnabled = bDefaultReceiveDMXEnabled;
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
void UDMXProtocolSettings::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent)
{
	const FName PropertyName = PropertyChangedChainEvent.GetPropertyName();
	const FProperty* Property = PropertyChangedChainEvent.Property;
	const UScriptStruct* InputPortConfigStruct = FDMXInputPortConfig::StaticStruct();
	const UScriptStruct* OutputPortConfigStruct = FDMXOutputPortConfig::StaticStruct();
	const UStruct* PropertyOwnerStruct = Property ? Property->GetOwnerStruct() : nullptr;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, FixtureCategories))
	{
		if (FixtureCategories.Num() == 0)
		{
			FixtureCategories.Add(TEXT("Other"));
		}

		FDMXFixtureCategory::OnValuesChanged.Broadcast();
	}
	else if (
		PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, Attributes) ||
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
	else if	(
		PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, InputPortConfigs) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, OutputPortConfigs) ||
		(InputPortConfigStruct == PropertyOwnerStruct || OutputPortConfigStruct || PropertyOwnerStruct)
		)
	{
		// if a new config was added, create a guid for that. We cannot do that in the ctor because 
		// the engine expects clearly defined default values, FGuid::NewGuid doesn't work with that.
		if (PropertyChangedChainEvent.ChangeType == EPropertyChangeType::ArrayAdd)
		{
			for (FDMXInputPortConfig& Config : InputPortConfigs)
			{
				if (!Config.GetPortGuid().IsValid())
				{
					Config = FDMXInputPortConfig(FGuid::NewGuid());
				}
			}

			for (FDMXOutputPortConfig& Config : OutputPortConfigs)
			{
				if (!Config.GetPortGuid().IsValid())
				{
					Config = FDMXOutputPortConfig(FGuid::NewGuid());
				}
			}
		}

		FDMXPortManager::Get().UpdateFromProtocolSettings();
	}
	
	Super::PostEditChangeChainProperty(PropertyChangedChainEvent);
}
#endif // WITH_EDITOR

void UDMXProtocolSettings::OverrideSendDMXEnabled(bool bEnabled) 
{
	bOverrideSendDMXEnabled = bEnabled; 
	
	OnSetSendDMXEnabled.Broadcast(bEnabled);
}

void UDMXProtocolSettings::OverrideReceiveDMXEnabled(bool bEnabled) 
{ 
	bOverrideReceiveDMXEnabled = bEnabled; 

	OnSetReceiveDMXEnabled.Broadcast(bEnabled);
}

