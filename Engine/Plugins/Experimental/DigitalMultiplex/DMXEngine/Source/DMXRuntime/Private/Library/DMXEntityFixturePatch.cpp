// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/DMXEntityFixturePatch.h"

#include "Library/DMXLibrary.h"
#include "Library/DMXEntityController.h"
#include "Library/DMXEntityFixtureType.h"
#include "DMXProtocolConstants.h"

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

	return 1;
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

TMap<FName, int32> UDMXEntityFixturePatch::GetFunctionDefaultMap() const
{
	TMap<FName, int32> DefaultValueMap;

	if (!CanReadActiveMode())
	{
		return DefaultValueMap;
	}

	const FDMXFixtureMode& Mode = ParentFixtureTypeTemplate->Modes[ActiveMode];
	const TArray<FDMXFixtureFunction>& Functions = Mode.Functions;
	DefaultValueMap.Reserve(Functions.Num());
	for (const FDMXFixtureFunction& Function : Functions)
	{
		if (UDMXEntityFixtureType::GetFunctionLastChannel(Function) <= Mode.ChannelSpan)
		{
			DefaultValueMap.Add(FName(*Function.FunctionName), Function.DefaultValue);
		}
	}
	return DefaultValueMap;
}

TMap<FName, int32> UDMXEntityFixturePatch::GetFunctionChannelAssignments() const
{
	TMap<FName, int32> ChannelMap;

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
			ChannelMap.Add(FName(*Function.FunctionName), Function.Channel);
		}
		ChannelMap.Add(FName(*Function.FunctionName), Function.Channel + GetStartingChannel() - 1);
	}
	return ChannelMap;
}

TMap<FName, EDMXFixtureSignalFormat> UDMXEntityFixturePatch::GetFunctionSignalFormats() const
{
	TMap<FName, EDMXFixtureSignalFormat> FormatMap;

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
			FormatMap.Add(FName(*Function.FunctionName), Function.DataType);
		}
	}
	return FormatMap;
}

TMap<FName, int32> UDMXEntityFixturePatch::ConvertRawMapToFunctionMap(const TMap<int32, uint8>& RawMap) const
{
	TMap<FName, int32> FunctionMap;

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

			FunctionMap.Add(*Function.FunctionName, IntVal);
		}
	}

	return FunctionMap;
}

TMap<int32, uint8> UDMXEntityFixturePatch::ConvertFunctionMapToRawMap(const TMap<FName, int32>& FunctionMap) const
{
	TMap<int32, uint8> RawMap;

	if (!CanReadActiveMode())
	{
		return RawMap;
	}

	const FDMXFixtureMode& Mode = ParentFixtureTypeTemplate->Modes[ActiveMode];
	const TArray<FDMXFixtureFunction>& Functions = Mode.Functions;

	RawMap.Reserve(FunctionMap.Num() * 4); // Let's assume all functions are 32bit. We can shrink RawMap later.

	for (const TPair<FName, int32>& Elem : FunctionMap)
	{
		// Search for a function with Elem.Key as name
		const FDMXFixtureFunction* FunctionPtr = Functions.FindByPredicate([&Elem] (const FDMXFixtureFunction& Function)
			{
				return Elem.Key == FName(*Function.FunctionName);
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

bool UDMXEntityFixturePatch::IsMapValid(const TMap<FName, int32>& FunctionMap) const
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

	for (const TPair<FName, int32>& Elem : FunctionMap)
	{
		if (!ContainsFunction(Elem.Key))
		{
			return false;
		}
	}
	return true;
}

TMap<FName, int32> UDMXEntityFixturePatch::ConvertToValidMap(const TMap<FName, int32>& FunctionMap) const
{
	TMap<FName, int32> ValidMap;

	if (!CanReadActiveMode())
	{
		return ValidMap;
	}

	const FDMXFixtureMode& Mode = ParentFixtureTypeTemplate->Modes[ActiveMode];
	const TArray<FDMXFixtureFunction>& Functions = Mode.Functions;
	ValidMap.Reserve(FunctionMap.Num());

	for (const TPair<FName, int32>& Elem : FunctionMap)
	{
		// Search for a function with Elem.Key as name
		const FDMXFixtureFunction* FunctionPtr = Functions.FindByPredicate([&Elem](const FDMXFixtureFunction& Function)
			{
				return Elem.Key == FName(*Function.FunctionName);
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

	return OutReason.IsEmpty();
}

void UDMXEntityFixturePatch::ValidateActiveMode()
{
	if (ParentFixtureTypeTemplate != nullptr)
	{
		ActiveMode = FMath::Clamp(ActiveMode, 0, ParentFixtureTypeTemplate->Modes.Num() - 1);
	}
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

UDMXEntityFixturePatch::UDMXEntityFixturePatch()
	: UniverseID(1)
	, bAutoAssignAddress(true)
	, ManualStartingAddress(1)
	, AutoStartingAddress(1)
	, ActiveMode(0)
{}

#undef LOCTEXT_NAMESPACE
