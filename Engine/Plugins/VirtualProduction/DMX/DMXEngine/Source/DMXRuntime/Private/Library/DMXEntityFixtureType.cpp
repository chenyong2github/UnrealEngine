// Copyright Epic Games, Inc. All Rights Reserved.


#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXImport.h"
#include "Library/DMXImportGDTF.h"
#include "DMXProtocolSettings.h"
#include "DMXRuntimeUtils.h"
#include "DMXUtils.h"


#if WITH_EDITOR
	/** Editor only data type change delegate */
FDataTypeChangeDelegate UDMXEntityFixtureType::DataTypeChangeDelegate;
#endif

#if WITH_EDITOR
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

		// Used to map Functions to Attributes
		const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();
		// Break the Attributes' keywords into arrays of strings to be read for each Function
		TMap< FName, TArray<FString> > AttributesKeywords;
		for (const FDMXAttribute& Attribute : ProtocolSettings->Attributes)
		{
			TArray<FString> Keywords = Attribute.GetKeywords();
			
			AttributesKeywords.Emplace(Attribute.Name, MoveTemp(Keywords));
		}

		// Copy modes from asset
		for (const FDMXImportGDTFDMXMode& AssetMode : GDTFDMXModes->DMXModes)
		{
			FDMXFixtureMode& Mode = Modes[Modes.Emplace()];
			Mode.ModeName = AssetMode.Name.ToString();

			// We'll keep the functions addresses from the GDTF file.
			// For that we need to keep track of the latest occupied address after adding each function.
			int32 LastOccupiedAddress = 0;

			// Keep track of the Attributes used on this Mode's Functions because they must be unique
			TArray<FName> MappedAttributes;

			for (const FDMXImportGDTFDMXChannel& ModeChannel : AssetMode.DMXChannels)
			{
				FDMXFixtureFunction& Function = Mode.Functions[Mode.Functions.Emplace()];
				Function.FunctionName = ModeChannel.LogicalChannel.Attribute.Name.ToString();
				Function.DefaultValue = ModeChannel.Default.Value;

				// Try to auto-map the Function to an existing Attribute
				// using the Function's name and the Attributes' keywords
				if (!Function.FunctionName.IsEmpty() && AttributesKeywords.Num())
				{
					// Remove white spaces and index numbers from the name
					FString FilteredFunctionName;
					int32 IndexFromName;
					FDMXRuntimeUtils::GetNameAndIndexFromString(Function.FunctionName, FilteredFunctionName, IndexFromName);

					// Check if the Function name matches any Attribute's keywords
					for (const TPair< FName, TArray<FString> >& Keywords : AttributesKeywords)
					{
						if (MappedAttributes.Contains(Keywords.Key))
						{
							continue; // Attribute already mapped to another Function in this Mode
						}

						auto CompareStringCaseInsensitive = [&FilteredFunctionName](const FString& Keyword)
						{
							return Keyword.Equals(FilteredFunctionName, ESearchCase::IgnoreCase);
						};

						// Match the Function name against the Attribute name and its keywords
						if (CompareStringCaseInsensitive(Keywords.Key.ToString())
							|| Keywords.Value.ContainsByPredicate(CompareStringCaseInsensitive))
						{
							Function.Attribute = Keywords.Key;
							MappedAttributes.Emplace(Keywords.Key); // Mark Attribute as used in this Mode
						}
					}
				}

				// Calculate Function's number of occupied channels/addresses
				if (ModeChannel.Offset.Num() > 0)
				{
					// Compute number of used addresses in the function as the interval
					// between the lowest and highest addresses (inclusive)
					int32 AddressMin = DMX_MAX_ADDRESS;
					int32 AddressMax = 0;
					for (const int32& Address : ModeChannel.Offset)
					{
						AddressMin = FMath::Min(AddressMin, Address);
						AddressMax = FMath::Max(AddressMax, Address);
					}
					const int32 NumUsedAddresses = FMath::Clamp(AddressMax - AddressMin + 1, 1, DMX_MAX_FUNCTION_SIZE);

					SetFunctionSize(Function, NumUsedAddresses);

					// AddressMin is the first address this function occupies. If it's not 1 after the
					// latest occupied channel, this function is offset, skipping some addresses.
					if (AddressMin > LastOccupiedAddress + 1)
					{
						Function.ChannelOffset = AddressMin - LastOccupiedAddress - 1;
					}

					// Offsets represent the value bytes in MSB format. If they are in reverse order,
					// it means this Function uses LSB format.
					// We need at least 2 offsets to compare. Otherwise, we leave the function as MSB,
					// which is most Fixtures' standard bit format.
					if (ModeChannel.Offset.Num() > 1)
					{
						Function.bUseLSBMode = ModeChannel.Offset[0] > ModeChannel.Offset[1];
					}
					else
					{
						Function.bUseLSBMode = false;
					}

					// Update occupied addresses on current mode
					LastOccupiedAddress += Function.ChannelOffset + NumChannelsToOccupy(Function.DataType);
				}
				else
				{
					SetFunctionSize(Function, 1);

					// Update occupied addresses
					++LastOccupiedAddress;
				}
			}

			// Compute mode channel span from functions' addresses and sizes
			UpdateModeChannelProperties(Mode);
		}
	}
}

void UDMXEntityFixtureType::SetFunctionSize(FDMXFixtureFunction& InFunction, uint8 Size)
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

	InFunction.DataType = NewDataType;
	ClampDefaultValue(InFunction);
}

#endif // WITH_EDITOR

bool FDMXPixelMatrix::GetChannelsFromPixel(FIntPoint Pixel, FDMXAttributeName Attribute, TArray<int32>& Channels) const
{
	Channels.Reset();

	TArray<int32> AllChannels;

	if (Pixel.X < 0 || Pixel.X >= XPixels)
	{
		return false;
	}

	if (Pixel.Y < 0 || Pixel.Y >= YPixels)
	{
		return false;
	}

	for (int32 YPixel = 0; YPixel < YPixels; YPixel++)
	{
		for (int32 XPixel = 0; XPixel < XPixels; XPixel++)
		{
			AllChannels.Add(XPixel + YPixel * XPixels);
		}
	}

	TArray<int32> OrderedChannels;
	FDMXUtils::PixelsDistributionSort(PixelsDistribution, XPixels, YPixels, AllChannels, OrderedChannels);

	check(AllChannels.Num() == OrderedChannels.Num());

	int32 PixelSize = 0;
	int32 PixelFunctionIndex = -1;
	int32 PixelFunctionSize = 0;
	for (const FDMXFixturePixelFunction& PixelFunction : PixelFunctions)
	{
		int32 CurrentFunctionSize = UDMXEntityFixtureType::NumChannelsToOccupy(PixelFunction.DataType);
		if (PixelFunction.Attribute.GetAttribute() == Attribute.GetAttribute())
		{
			PixelFunctionIndex = PixelSize;
			PixelFunctionSize = CurrentFunctionSize;
		}
		PixelSize += CurrentFunctionSize;
	}

	// no function found
	if (PixelFunctionIndex < 0 || PixelFunctionSize == 0)
	{
		return false;
	}

	int32 ChannelBase = FirstPixelChannel + (OrderedChannels[Pixel.Y + Pixel.X * YPixels] * PixelSize) + PixelFunctionIndex;

	for (int32 ChannelIndex = 0; ChannelIndex < PixelFunctionSize; ChannelIndex++)
	{
		Channels.Add(ChannelBase + ChannelIndex);
	}

	return true;
}

int32 FDMXPixelMatrix::GetPixelFunctionsLastChannel() const
{
	int32 PixelSize = 0;
	for (const FDMXFixturePixelFunction& PixelFunction : PixelFunctions)
	{
		int32 CurrentFunctionSize = UDMXEntityFixtureType::NumChannelsToOccupy(PixelFunction.DataType);
		PixelSize += CurrentFunctionSize;
	}
	int32 AllPixels = XPixels * YPixels * PixelSize;
	if (AllPixels == 0)
	{
		return FirstPixelChannel;
	}

	int32 LastChannel = FirstPixelChannel + AllPixels - 1;

	return LastChannel;
}

uint8 UDMXEntityFixtureType::GetFunctionLastChannel(const FDMXFixtureFunction& Function)
{
	return Function.Channel + UDMXEntityFixtureType::NumChannelsToOccupy(Function.DataType) - 1;
}

bool UDMXEntityFixtureType::IsFunctionInModeRange(const FDMXFixtureFunction& InFunction, const FDMXFixtureMode& InMode, int32 ChannelOffset /*= 0*/)
{
	const int32 LastChannel = GetFunctionLastChannel(InFunction);
	bool bLastChannelExceedsChannelSpan = LastChannel > InMode.ChannelSpan;
	bool bLastChannelExceedsUniverseSize = LastChannel + ChannelOffset > DMX_MAX_ADDRESS;

	return !bLastChannelExceedsChannelSpan && !bLastChannelExceedsUniverseSize;
}

bool UDMXEntityFixtureType::IsFixtureMatrixInModeRange(const FDMXPixelMatrix& InFixtureMatrix, const FDMXFixtureMode& InMode, int32 ChannelOffset /*= 0*/)
{
	const int32 LastChannel = InFixtureMatrix.GetPixelFunctionsLastChannel();
	bool bLastChannelExceedsChannelSpan = LastChannel > InMode.ChannelSpan;
	bool bLastChannelExceedsUniverseSize = LastChannel + ChannelOffset > DMX_MAX_ADDRESS;

	return !bLastChannelExceedsChannelSpan && !bLastChannelExceedsUniverseSize;
}

void UDMXEntityFixtureType::ClampDefaultValue(FDMXFixtureFunction& InFunction)
{
	InFunction.DefaultValue = ClampValueToDataType(InFunction.DataType, FMath::Min(InFunction.DefaultValue, (int64)MAX_uint32));
}

uint8 UDMXEntityFixtureType::NumChannelsToOccupy(EDMXFixtureSignalFormat DataType)
{
	switch (DataType)
	{
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

uint32 UDMXEntityFixtureType::GetDataTypeMaxValue(EDMXFixtureSignalFormat DataType)
{
	switch (DataType)
	{
	case EDMXFixtureSignalFormat::E8Bit:
		return MAX_uint8;
	case EDMXFixtureSignalFormat::E16Bit:
		return MAX_uint16;
	case EDMXFixtureSignalFormat::E24Bit:
		return 0xFFFFFF;
	case EDMXFixtureSignalFormat::E32Bit:
		return MAX_uint32;
	default:
		checkNoEntry();
		return 1;
	}
}

void UDMXEntityFixtureType::FunctionValueToBytes(const FDMXFixtureFunction& InFunction, uint32 InValue, uint8* OutBytes)
{
	IntToBytes(InFunction.DataType, InFunction.bUseLSBMode, InValue, OutBytes);
}

void UDMXEntityFixtureType::IntToBytes(EDMXFixtureSignalFormat InSignalFormat, bool bUseLSB, uint32 InValue, uint8* OutBytes)
{
	// Make sure the input value is in the valid range for the data type
	InValue = ClampValueToDataType(InSignalFormat, InValue);

	// Number of bytes we'll have to manipulate
	const uint8 NumBytes = NumChannelsToOccupy(InSignalFormat);

	if (NumBytes == 1)
	{
		OutBytes[0] = (uint8)InValue;
		return;
	}

	// To avoid branching in the loop, we'll decide before it on which byte to start
	// and which direction to go, depending on the Function's bit endianness.
	const int8 ByteIndexStep = bUseLSB ? 1 : -1;
	int8 OutByteIndex = bUseLSB ? 0 : NumBytes - 1;

	for (uint8 ValueByte = 0; ValueByte < NumBytes; ++ValueByte)
	{
		OutBytes[OutByteIndex] = InValue >> 8 * ValueByte & 0xFF;
		OutByteIndex += ByteIndexStep;
	}
}

uint32 UDMXEntityFixtureType::BytesToFunctionValue(const FDMXFixtureFunction& InFunction, const uint8* InBytes)
{
	return BytesToInt(InFunction.DataType, InFunction.bUseLSBMode, InBytes);
}

uint32 UDMXEntityFixtureType::BytesToInt(EDMXFixtureSignalFormat InSignalFormat, bool bUseLSB, const uint8* InBytes)
{
	// Number of bytes we'll read
	const uint8 NumBytes = NumChannelsToOccupy(InSignalFormat);

	if (NumBytes == 1)
	{
		return *InBytes;
	}

	// To avoid branching in the loop, we'll decide before it on which byte to start
	// and which direction to go, depending on the Function's bit endianness.
	const int8 ByteIndexStep = bUseLSB ? 1 : -1;
	int8 InByteIndex = bUseLSB ? 0 : NumBytes - 1;

	uint32 Result = 0;
	for (uint8 ValueByte = 0; ValueByte < NumBytes; ++ValueByte)
	{
		Result += InBytes[InByteIndex] << 8 * ValueByte;
		InByteIndex += ByteIndexStep;
	}

	return Result;
}

void UDMXEntityFixtureType::FunctionNormalizedValueToBytes(const FDMXFixtureFunction& InFunction, float InValue, uint8* OutBytes)
{
	NormalizedValueToBytes(InFunction.DataType, InFunction.bUseLSBMode, InValue, OutBytes);
}

void UDMXEntityFixtureType::NormalizedValueToBytes(EDMXFixtureSignalFormat InSignalFormat, bool bUseLSB, float InValue, uint8* OutBytes)
{
	// Make sure InValue is in the range [0.0 ... 1.0]
	InValue = FMath::Clamp(InValue, 0.0f, 1.0f);

	const uint32 IntValue = GetDataTypeMaxValue(InSignalFormat) * InValue;

	// Get the individual bytes from the computed IntValue
	IntToBytes(InSignalFormat, bUseLSB, IntValue, OutBytes);
}

float UDMXEntityFixtureType::BytesToFunctionNormalizedValue(const FDMXFixtureFunction& InFunction, const uint8* InBytes)
{
	return BytesToNormalizedValue(InFunction.DataType, InFunction.bUseLSBMode, InBytes);
}

float UDMXEntityFixtureType::BytesToNormalizedValue(EDMXFixtureSignalFormat InSignalFormat, bool bUseLSB, const uint8* InBytes)
{
	// Get the value represented by the individual bytes
	const float Value = BytesToInt(InSignalFormat, bUseLSB, InBytes);

	// Normalize it
	return Value / GetDataTypeMaxValue(InSignalFormat);
}

#if WITH_EDITOR
void UDMXEntityFixtureType::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName PropertyName = PropertyChangedEvent.GetPropertyName();
		
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, bPixelFunctionsEnabled))
	{				
		for (FDMXFixtureMode& Mode : Modes)
		{
			if (!bPixelFunctionsEnabled)
			{
				Mode.PixelMatrixConfig.PixelFunctions.Reset();
			}
			UpdateModeChannelProperties(Mode);
		}
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXEntityFixtureType::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
	FName PropertyName = PropertyChangedEvent.GetPropertyName();

	// Clamp DefaultValue from selected data type
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, DefaultValue) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, DataType))
	{
		const int32 ModeIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes).ToString());		

		check(ModeIndex != INDEX_NONE);

		for (FDMXFixtureFunction& Function : Modes[ModeIndex].Functions)
		{
			ClampDefaultValue(Function);
		}
	}

	// Refresh ChannelSpan from functions' settings
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, bPixelFunctionsEnabled) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXPixelMatrix, PixelFunctions) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXPixelMatrix, XPixels) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXPixelMatrix, YPixels) ||	
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXPixelMatrix, FirstPixelChannel) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, Functions) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, bAutoChannelSpan) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, ChannelOffset) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, DataType))
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
		
	// Keep Attributes unique across a Mode
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, Attribute))
	{
		// Find duplicates
		const int32 ModeIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes).ToString());
		check(ModeIndex != INDEX_NONE);

		FDMXFixtureMode& Mode = Modes[ModeIndex];

		const int32 ChangedFunctionIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, Functions).ToString());
		const int32 ChangedPixelFunctionIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_CHECKED(FDMXPixelMatrix, PixelFunctions).ToString());

		FDMXAttributeName ChangedAttribute;
		if (ChangedFunctionIndex != INDEX_NONE)
		{
			check(Mode.Functions.IsValidIndex(ChangedFunctionIndex));
			ChangedAttribute = Mode.Functions[ChangedFunctionIndex].Attribute;
		}
		else
		{
			check(Mode.PixelMatrixConfig.PixelFunctions.IsValidIndex(ChangedPixelFunctionIndex));
			ChangedAttribute = Mode.PixelMatrixConfig.PixelFunctions[ChangedPixelFunctionIndex].Attribute;
		}

		for (int32 IdxOtherFunction = 0; IdxOtherFunction < Mode.Functions.Num(); IdxOtherFunction++)
		{
			if (ChangedFunctionIndex == IdxOtherFunction)
			{
				continue;
			}

			FDMXAttributeName& Attribute = Mode.Functions[IdxOtherFunction].Attribute;
			if (ChangedAttribute == Attribute)
			{
				Attribute = FDMXAttributeName::None;
			}
		}

		int32 NumPixelFunctions = Mode.PixelMatrixConfig.PixelFunctions.Num();
		for (int32 IdxOtherPixelFunction = 0; IdxOtherPixelFunction < NumPixelFunctions; IdxOtherPixelFunction++)
		{
			if (ChangedPixelFunctionIndex == IdxOtherPixelFunction)
			{
				continue;
			}

			FDMXAttributeName& Attribute = Mode.PixelMatrixConfig.PixelFunctions[IdxOtherPixelFunction].Attribute;
			if (ChangedAttribute == Attribute)
			{
				Attribute = FDMXAttributeName::None;
			}
		}
	}

	// Keep Fixture Patches' ActiveMode value valid
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes) &&
		(PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayRemove || PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayClear))
	{		
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
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXEntityFixtureType::PostEditUndo()
{
	for (FDMXFixtureMode& Mode : Modes)
	{
		UpdateModeChannelProperties(Mode);
	}

	Super::PostEditUndo();
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXEntityFixtureType::UpdateModeChannelProperties(FDMXFixtureMode& Mode)
{
	if (Mode.bAutoChannelSpan)
	{
		if (Mode.Functions.Num() == 0 &&
			Mode.PixelMatrixConfig.PixelFunctions.Num() == 0)
		{
			Mode.ChannelSpan = 0;
		}
		else
		{

			int32 ChannelSpan = 0;

			// Update span from common Functions
			for (FDMXFixtureFunction& Function : Mode.Functions)
			{
				Function.Channel = ChannelSpan + 1 + Function.ChannelOffset;

				switch (Function.DataType)
				{
				case EDMXFixtureSignalFormat::E8Bit:
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

			// If pixel functions are enabled, add their channel span
			int32 NumPixels = Mode.PixelMatrixConfig.XPixels * Mode.PixelMatrixConfig.YPixels;
			if (bPixelFunctionsEnabled && NumPixels > 0)
			{
				// Add 'empty' channels bewtween normal functions and pixel functions to the channel span
				ChannelSpan = FMath::Max(ChannelSpan, Mode.PixelMatrixConfig.FirstPixelChannel);

				int32 PixelFunctionFirstChannel = Mode.PixelMatrixConfig.FirstPixelChannel;

				// Ignore channels that are overlapping commmon functions
				PixelFunctionFirstChannel = FMath::Max(PixelFunctionFirstChannel, ChannelSpan + 1);

				int32 PixelFunctionLastChannel = Mode.PixelMatrixConfig.GetPixelFunctionsLastChannel();

				ChannelSpan += PixelFunctionLastChannel - PixelFunctionFirstChannel + 1;
			}

			ChannelSpan = FMath::Max(ChannelSpan, 1);

			Mode.ChannelSpan = ChannelSpan;
		}

		// Notify DataType changes
		DataTypeChangeDelegate.Broadcast(this, Mode);
	}
}
#endif // WITH_EDITOR
