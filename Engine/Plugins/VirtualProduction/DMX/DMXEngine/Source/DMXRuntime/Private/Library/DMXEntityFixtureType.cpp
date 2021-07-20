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
int32 FDMXFixtureMode::AddOrInsertFunction(int32 IndexOfFunction, const FDMXFixtureFunction& InFunction)
{
	int32 Index = 0;
	FDMXFixtureFunction FunctionToAdd = InFunction;

	// Shift the insert function channel
	uint8 DataTypeBytes = UDMXEntityFixtureType::NumChannelsToOccupy(InFunction.DataType);
	FunctionToAdd.Channel = InFunction.Channel + DataTypeBytes;

	if (Functions.IsValidIndex(IndexOfFunction + 1))
	{
		Index = Functions.Insert(InFunction, IndexOfFunction + 1);

		// Shift all function after this by size of insert function
		for (int32 FunctionIndex = Index + 1; FunctionIndex < Functions.Num(); FunctionIndex++)
		{
			FDMXFixtureFunction& Function = Functions[FunctionIndex];
			Function.Channel = Function.Channel + DataTypeBytes;
		}
	}
	else
	{
		Index = Functions.Add(InFunction);
	}

	return Index;
}
#endif // WITH_EDITOR

bool FDMXFixtureMatrix::GetChannelsFromCell(FIntPoint CellCoordinate, FDMXAttributeName Attribute, TArray<int32>& Channels) const
{
	Channels.Reset();

	TArray<int32> AllChannels;

	if (CellCoordinate.X < 0 || CellCoordinate.X >= XCells)
	{
		return false;
	}

	if (CellCoordinate.Y < 0 || CellCoordinate.Y >= YCells)
	{
		return false;
	}

	for (int32 YCell = 0; YCell < YCells; YCell++)
	{
		for (int32 XCell = 0; XCell < XCells; XCell++)
		{
			AllChannels.Add(XCell + YCell * XCells);
		}
	}

	TArray<int32> OrderedChannels;
	FDMXRuntimeUtils::PixelMappingDistributionSort(PixelMappingDistribution, XCells, YCells, AllChannels, OrderedChannels);

	check(AllChannels.Num() == OrderedChannels.Num());

	int32 CellSize = 0;
	int32 CellAttributeIndex = -1;
	int32 CellAttributeSize = 0;
	for (const FDMXFixtureCellAttribute& CellAttribute : CellAttributes)
	{
		int32 CurrentFunctionSize = UDMXEntityFixtureType::NumChannelsToOccupy(CellAttribute.DataType);
		if (CellAttribute.Attribute.GetAttribute() == Attribute.GetAttribute())
		{
			CellAttributeIndex = CellSize;
			CellAttributeSize = CurrentFunctionSize;
		}
		CellSize += CurrentFunctionSize;
	}

	// no function found
	if (CellAttributeIndex < 0 || CellAttributeSize == 0)
	{
		return false;
	}

	int32 ChannelBase = FirstCellChannel + (OrderedChannels[CellCoordinate.Y + CellCoordinate.X * YCells] * CellSize) + CellAttributeIndex;

	for (int32 ChannelIndex = 0; ChannelIndex < CellAttributeSize; ChannelIndex++)
	{
		Channels.Add(ChannelBase + ChannelIndex);
	}

	return true;
}

int32 FDMXFixtureMatrix::GetFixtureMatrixLastChannel() const
{
	int32 CellAttributeSize = 0;
	for (const FDMXFixtureCellAttribute& Attribute : CellAttributes)
	{
		int32 CurrentFunctionSize = UDMXEntityFixtureType::NumChannelsToOccupy(Attribute.DataType);
		CellAttributeSize += CurrentFunctionSize;
	}
	int32 AllCells = XCells * YCells * CellAttributeSize;
	if (AllCells == 0)
	{
		return FirstCellChannel;
	}

	int32 LastChannel = FirstCellChannel + AllCells - 1;

	return LastChannel;
}

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

			// Get a unique name
			TMap<FString, uint32> PotentialFunctionNamesAndCount;
			for (const FDMXImportGDTFDMXChannel& ModeChannel : AssetMode.DMXChannels)
			{
				FString FunctionName = ModeChannel.LogicalChannel.Attribute.Name.ToString();

				PotentialFunctionNamesAndCount.Add(FunctionName, 0);
			}

			for (const FDMXImportGDTFDMXChannel& ModeChannel : AssetMode.DMXChannels)
			{
				FDMXFixtureFunction& Function = Mode.Functions[Mode.Functions.Emplace()];
				Function.FunctionName = FDMXRuntimeUtils::GenerateUniqueNameForImportFunction(PotentialFunctionNamesAndCount, ModeChannel.LogicalChannel.Attribute.Name.ToString());
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
			UpdateChannelSpan(Mode);
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

bool UDMXEntityFixtureType::IsFixtureMatrixInModeRange(const FDMXFixtureMatrix& InFixtureMatrix, const FDMXFixtureMode& InMode, int32 ChannelOffset /*= 0*/)
{
	const int32 LastChannel = InFixtureMatrix.GetFixtureMatrixLastChannel();
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
		// Unhandled type
		checkNoEntry();
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
		
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, bFixtureMatrixEnabled))
	{				
		for (FDMXFixtureMode& Mode : Modes)
		{
			if (!bFixtureMatrixEnabled)
			{
				Mode.FixtureMatrixConfig.CellAttributes.Reset();
			}
			UpdateChannelSpan(Mode);
		}
	}

	RebuildFixturePatchCaches();
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
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, bFixtureMatrixEnabled) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureMatrix, CellAttributes) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureMatrix, XCells) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureMatrix, YCells) ||	
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureMatrix, FirstCellChannel) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, Functions) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, bAutoChannelSpan) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, ChannelOffset) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, DataType))
	{
		// If we have a specific Modes index that was modified, update its properties
		const int32 ModeIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes).ToString());
		if (ModeIndex != INDEX_NONE)
		{
			if (PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureMatrix, XCells))
			{
				UpdateYCellsFromXCells(Modes[ModeIndex]);
			}
			else if (PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureMatrix, YCells))
			{
				UpdateXCellsFromYCells(Modes[ModeIndex]);
			}

			UpdateChannelSpan(Modes[ModeIndex]);
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
					if (PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureMatrix, XCells))
					{
						UpdateYCellsFromXCells(Mode);
					}
					else if (PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureMatrix, YCells))
					{
						UpdateXCellsFromYCells(Mode);
					}

					UpdateChannelSpan(Mode);
				}
			}
		}
	}
		
	// Keep Attributes unique across either Functions or Matrix Attributes
	// Note: The Attribute property exists in FDMXFixtureFunction and FDMXFixtureCellAttribute, the conditions cannot be separated.
	// In other words, the if statement here will be entered in both cases, even if either OR statement would be removed.
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, Attribute) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDMXFixtureCellAttribute, Attribute))
	{
		// Find duplicates
		const int32 ModeIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes).ToString());
		check(ModeIndex != INDEX_NONE);

		FDMXFixtureMode& Mode = Modes[ModeIndex];

		const int32 ChangedFunctionIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, Functions).ToString());
		const int32 ChangedCellAttributeIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_CHECKED(FDMXFixtureMatrix, CellAttributes).ToString());

		FDMXAttributeName ChangedAttribute;
		if (ChangedFunctionIndex != INDEX_NONE)
		{
			// Unique across functions
			check(Mode.Functions.IsValidIndex(ChangedFunctionIndex));
			ChangedAttribute = Mode.Functions[ChangedFunctionIndex].Attribute;

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
		}
		else
		{
			// Unique across fixture matrix attributes
			check(Mode.FixtureMatrixConfig.CellAttributes.IsValidIndex(ChangedCellAttributeIndex));
			ChangedAttribute = Mode.FixtureMatrixConfig.CellAttributes[ChangedCellAttributeIndex].Attribute;

			int32 NumCellAttributes = Mode.FixtureMatrixConfig.CellAttributes.Num();
			for (int32 IdxOtherCellAttribute = 0; IdxOtherCellAttribute < NumCellAttributes; IdxOtherCellAttribute++)
			{
				if (ChangedCellAttributeIndex == IdxOtherCellAttribute)
				{
					continue;
				}

				FDMXAttributeName& Attribute = Mode.FixtureMatrixConfig.CellAttributes[IdxOtherCellAttribute].Attribute;
				if (ChangedAttribute == Attribute)
				{
					Attribute = FDMXAttributeName::None;
				}
			}
		}
	}

	// Keep Fixture Patches' ActiveMode value valid
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes))
	{
		if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayRemove || PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayClear)
		{		
			if (ParentLibrary != nullptr)
			{
				ParentLibrary->ForEachEntityOfType<UDMXEntityFixturePatch>([this](UDMXEntityFixturePatch* Patch)
				{
					if (Patch->GetFixtureType() == this)
					{
						Patch->ValidateActiveMode();					
					}
				});
			}
		}

		int32 ChangedModeIndex = PropertyChangedEvent.GetArrayIndex(PropertyName.ToString());
		if (Modes.IsValidIndex(ChangedModeIndex))
		{
			// Notify DataType changes
			DataTypeChangeDelegate.Broadcast(this, Modes[ChangedModeIndex]);
		}
	}

	RebuildFixturePatchCaches();
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXEntityFixtureType::PostEditUndo()
{
	for (FDMXFixtureMode& Mode : Modes)
	{
		UpdateYCellsFromXCells(Mode);

		UpdateChannelSpan(Mode);
	}

	Super::PostEditUndo();

	RebuildFixturePatchCaches();
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXEntityFixtureType::UpdateModeChannelProperties(FDMXFixtureMode& Mode)
{
	// DEPRECATED 4.27
	UpdateChannelSpan(Mode);
	RebuildFixturePatchCaches();
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXEntityFixtureType::UpdateChannelSpan(FDMXFixtureMode& Mode)
{
	if (Mode.bAutoChannelSpan)
	{
		if (Mode.Functions.Num() == 0 &&
			Mode.FixtureMatrixConfig.CellAttributes.Num() == 0)
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

			// If fixture matrix is enabled, add the channel span of the matrix
			int32 NumCells = Mode.FixtureMatrixConfig.XCells * Mode.FixtureMatrixConfig.YCells;
			if (bFixtureMatrixEnabled && NumCells > 0)
			{
				// Add 'empty' channels bewtween normal functions and cell attributes to the channel span
				ChannelSpan = FMath::Max(ChannelSpan, Mode.FixtureMatrixConfig.FirstCellChannel);

				int32 FirstCellChannel = Mode.FixtureMatrixConfig.FirstCellChannel;

				// Ignore channels that are overlapping commmon functions
				FirstCellChannel = FMath::Max(FirstCellChannel, ChannelSpan + 1);

				int32 FixtureMatrixLastChannel = Mode.FixtureMatrixConfig.GetFixtureMatrixLastChannel();

				ChannelSpan += FixtureMatrixLastChannel - FirstCellChannel + 1;
			}

			ChannelSpan = FMath::Max(ChannelSpan, 1);
			Mode.ChannelSpan = ChannelSpan;
		}

		// Notify DataType changes
		DataTypeChangeDelegate.Broadcast(this, Mode);
	}

	RebuildFixturePatchCaches();
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXEntityFixtureType::UpdateYCellsFromXCells(FDMXFixtureMode& Mode)
{
	const int32 MaxNumCells = 512;

	Mode.FixtureMatrixConfig.XCells = FMath::Clamp(Mode.FixtureMatrixConfig.XCells, 1, MaxNumCells);
	Mode.FixtureMatrixConfig.YCells = FMath::Clamp(Mode.FixtureMatrixConfig.YCells, 1, MaxNumCells - Mode.FixtureMatrixConfig.XCells + 1);
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXEntityFixtureType::UpdateXCellsFromYCells(FDMXFixtureMode& Mode)
{
	const int32 MaxNumCells = 512;

	Mode.FixtureMatrixConfig.YCells = FMath::Clamp(Mode.FixtureMatrixConfig.YCells, 1, MaxNumCells);
	Mode.FixtureMatrixConfig.XCells = FMath::Clamp(Mode.FixtureMatrixConfig.XCells, 1, MaxNumCells - Mode.FixtureMatrixConfig.YCells + 1);
}
#endif // WITH_EDITOR

void UDMXEntityFixtureType::RebuildFixturePatchCaches()
{
	if (UDMXLibrary* Library = ParentLibrary.Get())
	{
		for (UDMXEntityFixturePatch* FixturePatch : Library->GetEntitiesTypeCast<UDMXEntityFixturePatch>())
		{
			if (FixturePatch->GetFixtureType() == this)
			{
				FixturePatch->RebuildCache();
			}
		}
	}
}

