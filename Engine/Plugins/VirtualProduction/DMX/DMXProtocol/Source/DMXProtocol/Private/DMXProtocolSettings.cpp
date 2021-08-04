// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolSettings.h"

#include "DMXProtocolBlueprintLibrary.h"
#include "DMXProtocolConstants.h"
#include "IO/DMXPortManager.h"
#include "Interfaces/IDMXProtocol.h"
#include "IO/DMXPortManager.h"


UDMXProtocolSettings::UDMXProtocolSettings()
	: SendingRefreshRate(DMX_RATE)
	, ReceivingRefreshRate_DEPRECATED(DMX_RATE)
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

	// Parse command line options for send and receive dmx
	if (FParse::Bool(FCommandLine::Get(), TEXT("DEFAULTSENDDMXENABLED="), bDefaultSendDMXEnabled))
	{
		UE_LOG(LogDMXProtocol, Log, TEXT("Overridden Default Send DMX Enabled from command line, set to %s."), bDefaultSendDMXEnabled ? TEXT("True") : TEXT("False"));
	}
	OverrideSendDMXEnabled(bDefaultSendDMXEnabled);

	if (FParse::Bool(FCommandLine::Get(), TEXT("DEFAULTRECEIVEDMXENABLED="), bDefaultReceiveDMXEnabled))
	{
		UE_LOG(LogDMXProtocol, Log, TEXT("Overridden Default Receive DMX Enabled from command line, set to %s."), bDefaultReceiveDMXEnabled ? TEXT("True") : TEXT("False"));
	}
	OverrideReceiveDMXEnabled(bDefaultReceiveDMXEnabled);	
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
		(InputPortConfigStruct && InputPortConfigStruct == PropertyOwnerStruct) ||
		(OutputPortConfigStruct && OutputPortConfigStruct == PropertyOwnerStruct))
	{
		if (PropertyChangedChainEvent.ChangeType == EPropertyChangeType::Duplicate)
		{
			// When duplicating configs, the guid will be duplicated, so we have to create unique ones instead

			int32 ChangedIndex = PropertyChangedChainEvent.GetArrayIndex(PropertyName.ToString());
			if (ensureAlways(ChangedIndex != INDEX_NONE))
			{
				if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, InputPortConfigs))
				{
					const int32 IndexOfDuplicate = InputPortConfigs.FindLastByPredicate([this, ChangedIndex](const FDMXInputPortConfig& InputPortConfig) {
						return InputPortConfigs[ChangedIndex].GetPortGuid() == InputPortConfig.GetPortGuid();
						});

					if (ensureAlways(IndexOfDuplicate != ChangedIndex))
					{
						InputPortConfigs[IndexOfDuplicate] = FDMXInputPortConfig(FGuid::NewGuid(), InputPortConfigs[ChangedIndex]);
					}
				}
				else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, OutputPortConfigs))
				{
					const int32 IndexOfDuplicate = OutputPortConfigs.FindLastByPredicate([this, ChangedIndex](const FDMXOutputPortConfig& OutputPortConfig) {
						return OutputPortConfigs[ChangedIndex].GetPortGuid() == OutputPortConfig.GetPortGuid();
						});

					if (ensureAlways(IndexOfDuplicate != ChangedIndex))
					{
						OutputPortConfigs[IndexOfDuplicate] = FDMXOutputPortConfig(FGuid::NewGuid(), OutputPortConfigs[ChangedIndex]);
					}
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
