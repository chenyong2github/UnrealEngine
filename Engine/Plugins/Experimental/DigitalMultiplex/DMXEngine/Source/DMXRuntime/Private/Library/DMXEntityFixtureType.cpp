// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXImport.h"
#include "Library/DMXImportGDTF.h"

void UDMXEntityFixtureType::SetModesFromDMXImport(UDMXImport* DMXImportAsset)
{
	if (DMXImportAsset == nullptr || !DMXImportAsset->IsValidLowLevelFast())
	{
		return; 
	}

	if (UDMXImportGDTFDMXModes* GDTFDMXModes = Cast<UDMXImportGDTFDMXModes>(DMXImportAsset->DMXModes))
	{
		// Clear existing modes
		Modes.Empty(DMXImportAsset->DMXModes != nullptr ? GDTFDMXModes->DMXModes.Num() : 0);

		if (DMXImportAsset->DMXModes == nullptr) 
		{ 
			return; 
		}

		// Copy modes from asset
		for (const FDMXImportGDTFDMXMode& AssetMode : GDTFDMXModes->DMXModes)
		{
			FDMXFixtureMode& Mode = Modes[Modes.Emplace()];
			Mode.ModeName = AssetMode.Name.ToString();

			for (const FDMXImportGDTFDMXChannel& ModeChannel : AssetMode.DMXChannels)
			{
				FDMXFixtureFunction& Function = Mode.Functions[Mode.Functions.Emplace()];
				Function.FunctionName = ModeChannel.LogicalChannel.ChannelFunction.Name.ToString();
				SetFunctionSize(Mode, Function, ModeChannel.Offset.Num());
				Function.DefaultValue = ModeChannel.Default.Value;
			}
		}
	}
}

void UDMXEntityFixtureType::SetFunctionSize(FDMXFixtureMode& InMode, FDMXFixtureFunction& InFunction, uint8 Size)
{
	//Get New Data Type
	EDMXFixtureSignalFormat NewDataType;
	switch (Size)
	{
	case 0:
	case 1:
		NewDataType = EDMXFixtureSignalFormat::E8Bit;
		break;
	case 2:
		NewDataType = EDMXFixtureSignalFormat::E16Bit;
		break;
	case 3:
		NewDataType = EDMXFixtureSignalFormat::E24Bit;
		break;
	case 4:
	default:
		NewDataType = EDMXFixtureSignalFormat::E32Bit;
		break;
	}

#if WITH_EDITOR
	//Update UI
	InFunction.DataType = NewDataType;
	UpdateModeChannelProperties(InMode);
	ClampDefaultValue(InFunction);
#endif // WITH_EDITOR
}

uint8 UDMXEntityFixtureType::GetFunctionLastChannel(const FDMXFixtureFunction& Function)
{
	return Function.Channel + UDMXEntityFixtureType::NumChannelsToOccupy(Function.DataType) - 1;
}

void UDMXEntityFixtureType::ClampDefaultValue(FDMXFixtureFunction& InFunction)
{
	int64& DefaultValue = InFunction.DefaultValue;
	switch (InFunction.DataType)
	{
	case EDMXFixtureSignalFormat::E8BitSubFunctions:
	case EDMXFixtureSignalFormat::E8Bit:
		DefaultValue = FMath::Clamp(DefaultValue, (int64)0, (int64)MAX_uint8);
		break;
	case EDMXFixtureSignalFormat::E16Bit:
		DefaultValue = FMath::Clamp(DefaultValue, (int64)0, (int64)MAX_uint16);
		break;
	case EDMXFixtureSignalFormat::E24Bit:
		DefaultValue = FMath::Clamp(DefaultValue, (int64)0, (int64)0xFFFFFF);
		break;
	case EDMXFixtureSignalFormat::E32Bit:
		DefaultValue = FMath::Clamp(DefaultValue, (int64)0, (int64)MAX_uint32);
		break;
	default:
		checkNoEntry();
		break;
	}
}

uint8 UDMXEntityFixtureType::NumChannelsToOccupy(EDMXFixtureSignalFormat DataType)
{
	switch (DataType)
	{
	case EDMXFixtureSignalFormat::E8BitSubFunctions:
	case EDMXFixtureSignalFormat::E8Bit:
		return 1;

	case EDMXFixtureSignalFormat::E16Bit:
		return 2;

	case EDMXFixtureSignalFormat::E24Bit:
		return 3;

	case EDMXFixtureSignalFormat::E32Bit:
		return 4;

	default:
		break;
	}
	return 1;
}

uint32 UDMXEntityFixtureType::ClampValueToDataType(EDMXFixtureSignalFormat DataType, uint32 InValue)
{
	switch (DataType)
	{
	case EDMXFixtureSignalFormat::E8BitSubFunctions:
	case EDMXFixtureSignalFormat::E8Bit:
		return FMath::Clamp(InValue, 0u, (uint32)MAX_uint8);

	case EDMXFixtureSignalFormat::E16Bit:
		return FMath::Clamp(InValue, 0u, (uint32)MAX_uint16);

	case EDMXFixtureSignalFormat::E24Bit:
		return FMath::Clamp(InValue, 0u, 0xFFFFFFu);

	case EDMXFixtureSignalFormat::E32Bit:
		return FMath::Clamp(InValue, 0u, MAX_uint32);

	default:
		break;
	}
	return InValue;
}

#if WITH_EDITOR

void UDMXEntityFixtureType::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	// Clamp DefaultValue from selected data type
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, DefaultValue)
		|| PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, DataType))
	{
		const int32 ModeIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes).ToString());
		const int32 FunctionIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, Functions).ToString());

		check(ModeIndex != INDEX_NONE && FunctionIndex != INDEX_NONE);
		FDMXFixtureFunction& Function = Modes[ModeIndex].Functions[FunctionIndex];

		ClampDefaultValue(Function);
	}

	// Refresh ChannelSpan from functions' settings
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, bAutoChannelSpan)
		|| PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, Functions)
		|| PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, ChannelOffset)
		|| PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, DataType))
	{
		// If we have a specific Modes index that was modified, update its properties
		const int32 ModeIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes).ToString());
		if (ModeIndex != INDEX_NONE)
		{
			UpdateModeChannelProperties(Modes[ModeIndex]);
		}
		else
		{
			// Unfortunately, some operations like reordering an array's values, don't give us an array index

			// Check if this is a property contained in a Modes property
			const auto HeadNode = PropertyChangedEvent.PropertyChain.GetHead();
			if (HeadNode != nullptr && HeadNode->GetValue() != nullptr
				&& HeadNode->GetValue()->GetFName() == GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes))
			{
				for (FDMXFixtureMode& Mode : Modes)
				{
					UpdateModeChannelProperties(Mode);
				}
			}
		}
	}

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes) &&
        (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayRemove || PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayClear))
	{
		// Warn patches from this type about the Mode(s) removal so they can keep their ActiveMode value valid
		if (ParentLibrary != nullptr)
		{
			ParentLibrary->ForEachEntityOfType<UDMXEntityFixturePatch>([this](UDMXEntityFixturePatch* Patch)
				{
					if (Patch->ParentFixtureTypeTemplate == this)
					{
						Patch->ValidateActiveMode();
					}
				});
		}
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}

void UDMXEntityFixtureType::PostEditUndo()
{
	for (FDMXFixtureMode& Mode : Modes)
	{
		UpdateModeChannelProperties(Mode);
	}

	Super::PostEditUndo();
}

void UDMXEntityFixtureType::UpdateModeChannelProperties(FDMXFixtureMode& Mode)
{
	int32 ChannelSpan = 0;

	for (FDMXFixtureFunction& Function : Mode.Functions)
	{
		Function.Channel = ChannelSpan + 1 + Function.ChannelOffset;

		switch (Function.DataType)
		{
		case EDMXFixtureSignalFormat::E8Bit:
		case EDMXFixtureSignalFormat::E8BitSubFunctions:
			ChannelSpan = Function.Channel;
			break;
		case EDMXFixtureSignalFormat::E16Bit:
			ChannelSpan = Function.Channel + 1;
			break;
		case EDMXFixtureSignalFormat::E24Bit:
			ChannelSpan = Function.Channel + 2;
			break;
		case EDMXFixtureSignalFormat::E32Bit:
			ChannelSpan = Function.Channel + 3;
			break;
		default:
			checkNoEntry();
			break;
		}
	}

	if (ChannelSpan < 1)
	{
		ChannelSpan = 1; 
	}
	if (ChannelSpan > 512)
	{
		ChannelSpan = 512; 
	}

	if (Mode.bAutoChannelSpan)
	{
		Mode.ChannelSpan = ChannelSpan;
	}
}

#endif // WITH_EDITOR