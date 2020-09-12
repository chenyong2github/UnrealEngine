// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/DMXEntityFixturePatch.h"

#include "Library/DMXLibrary.h"
#include "Library/DMXEntityController.h"
#include "Library/DMXEntityFixtureType.h"
#include "DMXProtocolConstants.h"
#include "Interfaces/IDMXProtocol.h"

#define LOCTEXT_NAMESPACE "DMXEntityFixturePatch"

DECLARE_LOG_CATEGORY_CLASS(DMXEntityFixtureTypePatchLog, Log, All);

int32 UDMXEntityFixturePatch::GetChannelSpan() const
{
	if (ParentFixtureTypeTemplate != nullptr)
	{
		// Check for existing modes before trying to read them
		const int32 NumModes = ParentFixtureTypeTemplate->Modes.Num();
		if (NumModes > 0 && ActiveMode < NumModes)
		{
			// Number of channels occupied by all of the Active Mode's functions
			return ParentFixtureTypeTemplate->Modes[ActiveMode].ChannelSpan;
		}
	}

	return 0;
}

int32 UDMXEntityFixturePatch::GetStartingChannel() const
{
	if (bAutoAssignAddress)
	{
		return AutoStartingAddress;
	}
	else
	{
		return ManualStartingAddress;
	}
}

int32 UDMXEntityFixturePatch::GetEndingChannel() const
{
	return GetStartingChannel() + GetChannelSpan() - 1;
}

int32 UDMXEntityFixturePatch::GetRemoteUniverse() const
{
	if (GetRelevantControllers().Num() > 0)
	{
		return GetRelevantControllers()[0]->RemoteOffset + UniverseID;
	}
	return -1;
}

TArray<FName> UDMXEntityFixturePatch::GetAllFunctionsInActiveMode() const
{
	TArray<FName> NameArray;

	if (!CanReadActiveMode())
	{
		return NameArray;
	}

	const FDMXFixtureMode& Mode = ParentFixtureTypeTemplate->Modes[ActiveMode];
	const TArray<FDMXFixtureFunction>& Functions = Mode.Functions;
	NameArray.Reserve(Functions.Num());
	for (const FDMXFixtureFunction& Function : Functions)
	{
		if (UDMXEntityFixtureType::GetFunctionLastChannel(Function) <= Mode.ChannelSpan)
		{
			NameArray.Add(FName(*Function.FunctionName));
		}
	}
	return NameArray;
}

TArray<FDMXAttributeName> UDMXEntityFixturePatch::GetAllAttributesInActiveMode() const
{
	TArray<FDMXAttributeName> NameArray;

	if (!CanReadActiveMode())
	{
		return NameArray;
	}

	const FDMXFixtureMode& Mode = ParentFixtureTypeTemplate->Modes[ActiveMode];
	const TArray<FDMXFixtureFunction>& Functions = Mode.Functions;
	NameArray.Reserve(Functions.Num());
	for (const FDMXFixtureFunction& Function : Functions)
	{
		if (UDMXEntityFixtureType::GetFunctionLastChannel(Function) <= Mode.ChannelSpan)
		{
			NameArray.Add(Function.Attribute);
		}
	}
	return NameArray;
}

TMap<FName, FDMXAttributeName> UDMXEntityFixturePatch::GetFunctionAttributesMap() const
{
	TMap<FName, FDMXAttributeName> AttributeMap;

	if (!CanReadActiveMode())
	{
		return AttributeMap;
	}

	const FDMXFixtureMode& Mode = ParentFixtureTypeTemplate->Modes[ActiveMode];
	const TArray<FDMXFixtureFunction>& Functions = Mode.Functions;
	AttributeMap.Reserve(Functions.Num());
	for (const FDMXFixtureFunction& Function : Functions)
	{
		if (UDMXEntityFixtureType::GetFunctionLastChannel(Function) <= Mode.ChannelSpan)
		{
			AttributeMap.Add(FName(*Function.FunctionName), Function.Attribute);
		}
	}
	return AttributeMap;
}

TMap<FDMXAttributeName, FDMXFixtureFunction> UDMXEntityFixturePatch::GetAttributeFunctionsMap() const
{
	TMap<FDMXAttributeName, FDMXFixtureFunction> FunctionMap;

	if (!CanReadActiveMode())
	{
		return FunctionMap;
	}

	const FDMXFixtureMode& Mode = ParentFixtureTypeTemplate->Modes[ActiveMode];
	const TArray<FDMXFixtureFunction>& Functions = Mode.Functions;
	FunctionMap.Reserve(Functions.Num());
	for (const FDMXFixtureFunction& Function : Functions)
	{
		if (UDMXEntityFixtureType::GetFunctionLastChannel(Function) <= Mode.ChannelSpan)
		{
			FunctionMap.Add(Function.Attribute, Function);
		}
	}
	return FunctionMap;
}

TMap<FDMXAttributeName, int32> UDMXEntityFixturePatch::GetAttributeDefaultMap() const
{
	TMap<FDMXAttributeName, int32> DefaultValueMap;

	if (!CanReadActiveMode())
	{
		return DefaultValueMap;
	}

	const FDMXFixtureMode& Mode = ParentFixtureTypeTemplate->Modes[ActiveMode];
	const TArray<FDMXFixtureFunction>& Functions = Mode.Functions;
	for (const FDMXFixtureFunction& Function : Functions)
	{
		if (UDMXEntityFixtureType::GetFunctionLastChannel(Function) <= Mode.ChannelSpan)
		{
			DefaultValueMap.Add(Function.Attribute, Function.DefaultValue);
		}
	}
	
	return DefaultValueMap;
}

TMap<FDMXAttributeName, int32> UDMXEntityFixturePatch::GetAttributeChannelAssignments() const
{
	TMap<FDMXAttributeName, int32> ChannelMap;

	if (!CanReadActiveMode())
	{
		return ChannelMap;
	}

	const FDMXFixtureMode& Mode = ParentFixtureTypeTemplate->Modes[ActiveMode];
	const TArray<FDMXFixtureFunction>& Functions = Mode.Functions;
	ChannelMap.Reserve(Functions.Num());
	for (const FDMXFixtureFunction& Function : Functions)
	{
		if (UDMXEntityFixtureType::GetFunctionLastChannel(Function) <= Mode.ChannelSpan)
		{
			ChannelMap.Add(Function.Attribute, Function.Channel);
		}
		ChannelMap.Add(Function.Attribute, Function.Channel + GetStartingChannel() - 1);
	}
	return ChannelMap;
}

TMap<FDMXAttributeName, EDMXFixtureSignalFormat> UDMXEntityFixturePatch::GetAttributeSignalFormats() const
{
	TMap<FDMXAttributeName, EDMXFixtureSignalFormat> FormatMap;

	if (!CanReadActiveMode())
	{
		return FormatMap;
	}

	const FDMXFixtureMode& Mode = ParentFixtureTypeTemplate->Modes[ActiveMode];
	const TArray<FDMXFixtureFunction>& Functions = Mode.Functions;
	FormatMap.Reserve(Functions.Num());
	for (const FDMXFixtureFunction& Function : Functions)
	{
		if (UDMXEntityFixtureType::GetFunctionLastChannel(Function) <= Mode.ChannelSpan)
		{
			FormatMap.Add(Function.Attribute, Function.DataType);
		}
	}
	return FormatMap;
}

TMap<FDMXAttributeName, int32> UDMXEntityFixturePatch::ConvertRawMapToAttributeMap(const TMap<int32, uint8>& RawMap) const
{
	TMap<FDMXAttributeName, int32> FunctionMap;

	if (!CanReadActiveMode())
	{
		return FunctionMap;
	}

	const FDMXFixtureMode& Mode = ParentFixtureTypeTemplate->Modes[ActiveMode];
	const TArray<FDMXFixtureFunction>& Functions = Mode.Functions;
	// Let's consider all functions in the raw map are 8bit and allocate for 1 channel = 1 function
	// We'll avoid many allocations but potentially have a map 4x the size we need. We can shrink it later
	FunctionMap.Reserve(RawMap.Num());

	for (const FDMXFixtureFunction& Function : Functions)
	{
		// Ignore functions outside the Active Mode's Channel Span
		if (UDMXEntityFixtureType::GetFunctionLastChannel(Function) <= Mode.ChannelSpan
			&& RawMap.Contains(Function.Channel))
		{
			const uint8& RawValue(RawMap.FindRef(Function.Channel));
			const uint8 ChannelsToAdd = UDMXEntityFixtureType::NumChannelsToOccupy(Function.DataType);

			uint32 IntVal = 0;
			for (uint8 ChannelIt = 0; ChannelIt < ChannelsToAdd; ChannelIt++)
			{
				if (const uint8* RawVal = RawMap.Find(Function.Channel + ChannelIt))
				{
					IntVal += *RawVal << (ChannelIt * 8);
				}
			}

			FunctionMap.Add(Function.Attribute, IntVal);
		}
	}

	return FunctionMap;
}

TMap<int32, uint8> UDMXEntityFixturePatch::ConvertAttributeMapToRawMap(const TMap<FDMXAttributeName, int32>& FunctionMap) const
{
	TMap<int32, uint8> RawMap;

	if (!CanReadActiveMode())
	{
		return RawMap;
	}

	const FDMXFixtureMode& Mode = ParentFixtureTypeTemplate->Modes[ActiveMode];
	const TArray<FDMXFixtureFunction>& Functions = Mode.Functions;

	RawMap.Reserve(FunctionMap.Num() * 4); // Let's assume all functions are 32bit. We can shrink RawMap later.

	for (const TPair<FDMXAttributeName, int32>& Elem : FunctionMap)
	{
		// Search for a function with Attribute == Elem.Key.Attribute
		const FDMXFixtureFunction* FunctionPtr = Functions.FindByPredicate([&Elem](const FDMXFixtureFunction& Function)
		{
			return Elem.Key == Function.Attribute;
		});

		// Also check for the Function being in the valid range for the Active Mode's channel span
		if (FunctionPtr != nullptr && Mode.ChannelSpan >= UDMXEntityFixtureType::GetFunctionLastChannel(*FunctionPtr))
		{
			const uint8&& NumChannels = UDMXEntityFixtureType::NumChannelsToOccupy(FunctionPtr->DataType);
			const uint32 Value = UDMXEntityFixtureType::ClampValueToDataType(FunctionPtr->DataType, Elem.Value);

			for (uint8 ChannelIt = 0; ChannelIt < NumChannels; ChannelIt++)
			{
				const uint8 BytesOffset = (ChannelIt) * 8;
				const uint8 ChannelVal = Value >> BytesOffset & 0xff;

				const int32 FinalChannel = FunctionPtr->Channel + (GetStartingChannel() - 1);
				RawMap.Add(FinalChannel + ChannelIt, ChannelVal);
			}
		}
	}

	RawMap.Shrink();
	return RawMap;
}

bool UDMXEntityFixturePatch::IsMapValid(const TMap<FDMXAttributeName, int32>& FunctionMap) const
{
	if (!CanReadActiveMode())
	{
		if (FunctionMap.Num() == 0)
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	const TArray<FDMXFixtureFunction>& Functions = ParentFixtureTypeTemplate->Modes[ActiveMode].Functions;

	for (const TPair<FDMXAttributeName, int32>& Elem : FunctionMap)
	{
		if (!ContainsAttribute(Elem.Key))
		{
			return false;
		}
	}
	return true;
}

TMap<FDMXAttributeName, int32> UDMXEntityFixturePatch::ConvertToValidMap(const TMap<FDMXAttributeName, int32>& FunctionMap) const
{
	TMap<FDMXAttributeName, int32> ValidMap;

	if (!CanReadActiveMode())
	{
		return ValidMap;
	}

	const FDMXFixtureMode& Mode = ParentFixtureTypeTemplate->Modes[ActiveMode];
	const TArray<FDMXFixtureFunction>& Functions = Mode.Functions;
	ValidMap.Reserve(FunctionMap.Num());

	for (const TPair<FDMXAttributeName, int32>& Elem : FunctionMap)
	{
		// Search for a function with Attribute == Elem.Key.Attribute
		const FDMXFixtureFunction* FunctionPtr = Functions.FindByPredicate([&Elem](const FDMXFixtureFunction& Function)
		{
			return Elem.Key == Function.Attribute;
		});

		// Also check for the Function being in the valid range for the Active Mode's channel span
		if (FunctionPtr != nullptr && Mode.ChannelSpan >= UDMXEntityFixtureType::GetFunctionLastChannel(*FunctionPtr))
		{
			ValidMap.Add(Elem.Key, Elem.Value);
		}
	}

	ValidMap.Shrink();
	return ValidMap;
}

bool UDMXEntityFixturePatch::IsValidEntity(FText& OutReason) const
{
	OutReason = FText::GetEmpty();

	if (ParentFixtureTypeTemplate == nullptr)
	{
		OutReason = LOCTEXT("InvalidReason_NullParentTemplate", "Fixture Template is null");
	}
	else if ((GetStartingChannel() + GetChannelSpan() - 1) > DMX_UNIVERSE_SIZE)
	{
		OutReason = FText::Format(
			LOCTEXT("InvalidReason_ChannelOverflow", "Channels range overflows max channel address ({0})"),
			FText::AsNumber(DMX_UNIVERSE_SIZE)
		);
	}
	else if (ParentFixtureTypeTemplate->Modes.Num() == 0)
	{
		OutReason = FText::Format(
			LOCTEXT("InvalidReason_NoModesDefined", "'{0}' cannot be assigned as its parent Fixture Type '{1}' does not define any Modes."),			
			FText::FromString(GetDisplayName()),
			FText::FromString(ParentFixtureTypeTemplate->GetDisplayName())
		);
	}	
	else
	{
		int32 IdxExistingFunction = ParentFixtureTypeTemplate->Modes.IndexOfByPredicate([](const FDMXFixtureMode& Mode) {
			return 
				Mode.Functions.Num() > 0 ||
				Mode.PixelMatrixConfig.PixelFunctions.Num() > 0;
			});

		if (IdxExistingFunction == INDEX_NONE)
		{
			OutReason = FText::Format(
				LOCTEXT("InvalidReason_NoFunctionsDefined", "'{0}' cannot be assigned as its parent Fixture Type '{1}' does not define any Functions."),
				FText::FromString(GetDisplayName()),
				FText::FromString(ParentFixtureTypeTemplate->GetDisplayName()));
		}
		else if (GetChannelSpan() == 0)
		{
			OutReason = FText::Format(
				LOCTEXT("InvalidReason_ParentChannelSpanIsZero", "'{0}' cannot be assigned as its parent Fixture Type '{1}' overrides channel span with 0."),
				FText::FromString(GetDisplayName()),
				FText::FromString(ParentFixtureTypeTemplate->GetDisplayName()));
		}
	}

	return OutReason.IsEmpty();
}

void UDMXEntityFixturePatch::ValidateActiveMode()
{
	if (ParentFixtureTypeTemplate != nullptr)
	{
		ActiveMode = FMath::Clamp(ActiveMode, 0, ParentFixtureTypeTemplate->Modes.Num() - 1);
	}
}

const FDMXFixtureFunction* UDMXEntityFixturePatch::GetAttributeFunction(const FDMXAttributeName& Attribute) const
{
	if (!CanReadActiveMode())
	{
		UE_LOG(DMXEntityFixtureTypePatchLog, Warning, TEXT("%S: Can't read the active Mode from the Fixture Type"), __FUNCTION__);
		return nullptr;
	}

	// No need for any search if there are no Functions on the active Mode.
	if (ParentFixtureTypeTemplate->Modes[ActiveMode].Functions.Num() == 0)
	{
		return nullptr;
	}

	const FDMXFixtureMode& Mode = ParentFixtureTypeTemplate->Modes[ActiveMode];
	const int32 FixtureChannelStart = GetStartingChannel() - 1;

	// Search the Function mapped to the selected Attribute
	for (const FDMXFixtureFunction& Function : Mode.Functions)
	{
		if (Function.Channel > DMX_MAX_ADDRESS)
		{
			// Following functions will have even higher Channels, so we can stop searching
			break;
		}

		if (!UDMXEntityFixtureType::IsFunctionInModeRange(Function, Mode, FixtureChannelStart))
		{
			// We reached the functions outside the valid channels for this mode
			break;
		}

		if (Function.Attribute == Attribute)
		{
			return &Function;
		}
	}

	return nullptr;
}

UDMXEntityController* UDMXEntityFixturePatch::GetFirstRelevantController() const
{
	if (ParentLibrary != nullptr)
	{
		UDMXEntityController* RelevantController = nullptr;
		ParentLibrary->ForEachEntityOfType<UDMXEntityController>([&](UDMXEntityController* Controller)
			{
				if (IsInControllerRange(Controller))
				{
					RelevantController = Controller;
					return;
				}
			});
		return RelevantController;
	}
	else
	{
		UE_LOG(DMXEntityFixtureTypePatchLog, Fatal, TEXT("Parent library is null!"));
	}
	return nullptr;
}

TArray<UDMXEntityController*> UDMXEntityFixturePatch::GetRelevantControllers() const
{
	TArray<UDMXEntityController*> RetVal;
	if (ParentLibrary != nullptr)
	{
		ParentLibrary->ForEachEntityOfType<UDMXEntityController>([&](UDMXEntityController* Controller)
		{
			if (IsInControllerRange(Controller))
			{
				RetVal.Add(Controller);
			}
		});
	}
	else
	{
		UE_LOG(DMXEntityFixtureTypePatchLog, Fatal, TEXT("Parent library is null!"));
	}

	return RetVal;
}

bool UDMXEntityFixturePatch::IsInControllersRange(const TArray<UDMXEntityController*>& InControllers) const
{
	for (const UDMXEntityController* Controller : InControllers)
	{
		if (IsInControllerRange(Controller))
		{
			return true;
		}
	}
	return false;
}

int32 UDMXEntityFixturePatch::GetAttributeValue(FDMXAttributeName Attribute, bool& bSuccess)
{
	bSuccess = false;

	const FDMXFixtureFunction* AttributeFunction = GetAttributeFunction(Attribute);
	if (AttributeFunction == nullptr)
	{
		return 0;
	}

	// Use the device protocol from the first Controller affecting this Patch
	UDMXEntityController* Controller = GetFirstRelevantController();
	if (Controller == nullptr)
	{
		UE_LOG(DMXEntityFixtureTypePatchLog, Warning, TEXT("%S: No valid Controller was found"), __FUNCTION__);
		return 0;
	}

	FDMXBufferPtr InputDMXBuffer = Controller->GetInputDMXBuffer(UniverseID);
	if (!InputDMXBuffer.IsValid())
	{
		UE_LOG(DMXEntityFixtureTypePatchLog, Warning, TEXT("%S: InputDMXBuffer Not Valid"), __FUNCTION__);
		return 0;
	}

	uint32 ResultValue = 0;
	InputDMXBuffer->AccessDMXData([&](TArray<uint8>& DMXData)
		{
			const int32 FixtureChannelStart = GetStartingChannel() - 1;
			const int32 FunctionStartIndex = AttributeFunction->Channel - 1 + FixtureChannelStart;
			const int32 FunctionLastIndex = FunctionStartIndex + UDMXEntityFixtureType::NumChannelsToOccupy(AttributeFunction->DataType) - 1;
			if (FunctionLastIndex >= DMXData.Num())
			{
				return;
			}

			ResultValue = UDMXEntityFixtureType::BytesToFunctionValue(*AttributeFunction, DMXData.GetData() + FunctionStartIndex);
			bSuccess = true;
		});

	return static_cast<int32>(ResultValue);
}

void UDMXEntityFixturePatch::GetAttributesValues(TMap<FDMXAttributeName, int32>& AttributesValues)
{
	AttributesValues.Reset();

	if (!CanReadActiveMode())
	{
		UE_LOG(DMXEntityFixtureTypePatchLog, Warning, TEXT("%S: Can't read the active Mode from the Fixture Type"), __FUNCTION__);
		return;
	}

	// No need for any search if there are no Functions on the active Mode.
	if (ParentFixtureTypeTemplate->Modes[ActiveMode].Functions.Num() == 0)
	{
		return;
	}

	// Use the device protocol from the first Controller affecting this Patch
	UDMXEntityController* Controller = GetFirstRelevantController();
	if (Controller == nullptr)
	{
		UE_LOG(DMXEntityFixtureTypePatchLog, Warning, TEXT("%S: No valid Controller was found"), __FUNCTION__);
		return;
	}

	FDMXBufferPtr InputDMXBuffer = Controller->GetInputDMXBuffer(UniverseID);
	if (!InputDMXBuffer.IsValid())
	{
		UE_LOG(DMXEntityFixtureTypePatchLog, Warning, TEXT("%S: InputDMXBuffer Not Valid"), __FUNCTION__);
		return;
	}

	const FDMXFixtureMode& Mode = ParentFixtureTypeTemplate->Modes[ActiveMode];
	const int32 FixtureChannelStart = GetStartingChannel() - 1;

	// Search the Function mapped to the selected Attribute
	for (const FDMXFixtureFunction& Function : Mode.Functions)
	{
		if (Function.Channel > DMX_MAX_ADDRESS)
		{
			// Following functions will have even higher Channels, so we can stop iterating
			break;
		}

		if (!UDMXEntityFixtureType::IsFunctionInModeRange(Function, Mode, FixtureChannelStart))
		{
			// We reached the functions outside the valid channels for this mode
			break;
		}

		if (FDMXAttributeName::IsValid(Function.Attribute.GetName()))
		{
			continue;
		}

		InputDMXBuffer->AccessDMXData([&](TArray<uint8>& DMXData)
		{
			const int32 FunctionStartIndex = Function.Channel - 1 + FixtureChannelStart;
			const int32 FunctionLastIndex = FunctionStartIndex + UDMXEntityFixtureType::NumChannelsToOccupy(Function.DataType) - 1;
			if (FunctionLastIndex >= DMXData.Num())
			{
				return;
			}

			const uint32 ResultValue = UDMXEntityFixtureType::BytesToFunctionValue(Function, DMXData.GetData() + FunctionStartIndex);
			AttributesValues.Add(Function.Attribute, ResultValue);
		});
	};
}

UDMXEntityFixturePatch::UDMXEntityFixturePatch()
	: UniverseID(1)
	, bAutoAssignAddress(true)
	, ManualStartingAddress(1)
	, AutoStartingAddress(1)
	, ActiveMode(0)
{}

#undef LOCTEXT_NAMESPACE
