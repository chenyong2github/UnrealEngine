// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/DMXEntityFixturePatch.h"

#include "DMXProtocolConstants.h"
#include "DMXRuntimeLog.h"
#include "DMXStats.h"
#include "DMXTypes.h"
#include "DMXUtils.h"
#include "Interfaces/IDMXProtocol.h"
#include "IO/DMXInputPort.h"
#include "IO/DMXOutputPort.h"
#include "Library/DMXEntityController.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXLibrary.h"

DECLARE_LOG_CATEGORY_CLASS(DMXEntityFixturePatchLog, Log, All);

DECLARE_CYCLE_STAT(TEXT("FixturePatch receive DMX"), STAT_DMXFixturePatchReceiveDMX, STATGROUP_DMX);
DECLARE_CYCLE_STAT(TEXT("FixturePatch cache values"), STAT_DMXFixturePatchCacheValues, STATGROUP_DMX);


#define LOCTEXT_NAMESPACE "DMXEntityFixturePatch"

UDMXEntityFixturePatch::UDMXEntityFixturePatch()
	: UniverseID(1)
	, bAutoAssignAddress(true)
	, ManualStartingAddress(1)
	, AutoStartingAddress(1)
	, ActiveMode(0)
#if WITH_EDITORONLY_DATA
	, EditorColor(FLinearColor(1.0f, 0.0f, 1.0f))
	, bReceiveDMXInEditor(false)
#endif // WITH_EDITORONLY_DATA
{
	CachedDMXValues.Reserve(DMX_UNIVERSE_SIZE);
}

#if WITH_EDITOR
void UDMXEntityFixturePatch::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityFixturePatch, UniverseID))
	{
		// Universe changed, so cached data is no longer valid
		ClearCachedData();
	}
}
#endif // WITH_EDITOR

void UDMXEntityFixturePatch::Tick(float DeltaTime)
{
	if (UpdateCachedValues())
	{
		OnFixturePatchReceivedDMX.Broadcast(this, CachedNormalizedValuesPerAttribute);
	}
}

bool UDMXEntityFixturePatch::IsTickable() const
{
	return OnFixturePatchReceivedDMX.IsBound();
}	

bool UDMXEntityFixturePatch::IsTickableInEditor() const
{
	const bool bHasListener = OnFixturePatchReceivedDMX.IsBound();

#if WITH_EDITORONLY_DATA
	return bHasListener && bReceiveDMXInEditor;
#endif
	return bHasListener; 
}

ETickableTickType UDMXEntityFixturePatch::GetTickableTickType() const
{
	return ETickableTickType::Conditional;
}

TStatId UDMXEntityFixturePatch::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FDMXInputPort, STATGROUP_Tickables);
}

void UDMXEntityFixturePatch::SendDMX(TMap<FDMXAttributeName, int32> AttributeMap)
{
	const FDMXFixtureMode* ModePtr = GetActiveMode();
	if (!ModePtr)
	{
		UE_LOG(LogDMXRuntime, Error, TEXT("Tried to send DMX via Fixture Patch %s, but its Parent Fixture Type has no Modes set up."), *GetName());
		return;
	}

	TMap<int32, uint8> DMXChannelToValueMap;
	for (const TPair<FDMXAttributeName, int32>& Elem : AttributeMap)
	{
		for (const FDMXFixtureFunction& Function : ModePtr->Functions)
		{
			const FDMXAttributeName FunctionAttr = Function.Attribute;
			if (FunctionAttr == Elem.Key)
			{
				if (!UDMXEntityFixtureType::IsFunctionInModeRange(Function, *ModePtr, GetStartingChannel() - 1))
				{
					continue;
				}

				const int32 Channel = Function.Channel + GetStartingChannel() - 1;

				uint32 ChannelValue = 0;
				uint8* ChannelValueBytes = reinterpret_cast<uint8*>(&ChannelValue);
				UDMXEntityFixtureType::FunctionValueToBytes(Function, Elem.Value, ChannelValueBytes);

				const uint8 NumBytesInSignalFormat = UDMXEntityFixtureType::NumChannelsToOccupy(Function.DataType);
				for (uint8 ChannelIt = 0; ChannelIt < NumBytesInSignalFormat; ++ChannelIt)
				{
					DMXChannelToValueMap.Add(Channel + ChannelIt, ChannelValueBytes[ChannelIt]);
				}
			}
		}
	}

	// Send to the library's output ports
	if (UDMXLibrary* DMXLibrary = ParentLibrary.Get())
	{
		for (const FDMXOutputPortSharedRef& OutputPort : DMXLibrary->GetOutputPorts())
		{
			OutputPort->SendDMX(UniverseID, DMXChannelToValueMap);
		}
	}
}

#if WITH_EDITOR
void UDMXEntityFixturePatch::ClearCachedData()
{
	LastDMXSignal.Reset();

	CachedDMXValues.Reset(DMX_UNIVERSE_SIZE);

	/** Map of normalized values per attribute, direct represpentation of CachedDMXValues. */
	CachedNormalizedValuesPerAttribute.Map.Reset();
}
#endif // WITH_EDITOR

bool UDMXEntityFixturePatch::UpdateCachedValues()
{
	SCOPE_CYCLE_COUNTER(STAT_DMXFixturePatchCacheValues);

	if (UDMXLibrary* DMXLibrary = ParentLibrary.Get())
	{
		// Get the lastest DMX signal
		FDMXSignalSharedPtr NewDMXSignal;
		for (const FDMXInputPortSharedRef& InputPort : DMXLibrary->GetInputPorts())
		{
			if(InputPort->GameThreadGetDMXSignal(UniverseID, NewDMXSignal))
			{
				if (!LastDMXSignal.IsValid() ||
					NewDMXSignal->Timestamp > LastDMXSignal->Timestamp)
				{
					LastDMXSignal = NewDMXSignal;
				}
			}
		}

		for (const FDMXOutputPortSharedRef& OutputPort : DMXLibrary->GetOutputPorts())
		{
			if(OutputPort->GameThreadGetDMXSignal(UniverseID, NewDMXSignal))
			{
				if (!LastDMXSignal.IsValid() ||
					NewDMXSignal->Timestamp > LastDMXSignal->Timestamp)
				{
					LastDMXSignal = NewDMXSignal;
				}
			}
		}

		if (LastDMXSignal.IsValid())
		{
			// Test if data changed
			const int32 StartingIndex = GetStartingChannel() - 1;
			const int32 NumChannels = GetChannelSpan();

			if (CachedDMXValues.Num() < NumChannels ||
				FMemory::Memcmp(CachedDMXValues.GetData(), &LastDMXSignal->ChannelData[StartingIndex], NumChannels) != 0)
			{
				// Update raw cache
				CachedDMXValues = TArray<uint8>(&LastDMXSignal->ChannelData[StartingIndex], NumChannels);

				// Update normalized cache
				CachedNormalizedValuesPerAttribute.Map.Reset();

				const FDMXFixtureMode* ModePtr = GetActiveMode();
				if (ModePtr)
				{
					for (const FDMXFixtureFunction& Function : ModePtr->Functions)
					{
						const int32 FunctionStartIndex = Function.Channel - 1;
						const int32 FunctionLastIndex = FunctionStartIndex + UDMXEntityFixtureType::NumChannelsToOccupy(Function.DataType) - 1;
						if (FunctionLastIndex >= CachedDMXValues.Num())
						{
							break;
						}

						const uint32 IntValue = UDMXEntityFixtureType::BytesToFunctionValue(Function, CachedDMXValues.GetData() + FunctionStartIndex);
						const float NormalizedValue = (float)IntValue / (float)UDMXEntityFixtureType::GetDataTypeMaxValue(Function.DataType);

						CachedNormalizedValuesPerAttribute.Map.Add(Function.Attribute, NormalizedValue);
					}

					return true;
				}
			}
		}
	}

	return false;
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
				Mode.FixtureMatrixConfig.CellAttributes.Num() > 0;
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

bool UDMXEntityFixturePatch::CanReadActiveMode() const
{
	// DEPRECATED 4.27
	return ParentFixtureTypeTemplate != nullptr
		&& ParentFixtureTypeTemplate->IsValidLowLevelFast()
		&& ParentFixtureTypeTemplate->Modes.IsValidIndex(ActiveMode);
}

const FDMXFixtureMode* UDMXEntityFixturePatch::GetActiveMode() const
{
	if (ParentFixtureTypeTemplate && 
		ParentFixtureTypeTemplate->IsValidLowLevelFast() &&
		ParentFixtureTypeTemplate->Modes.IsValidIndex(ActiveMode))
	{
		return &ParentFixtureTypeTemplate->Modes[ActiveMode];
	}

	return nullptr;
}

int32 UDMXEntityFixturePatch::GetChannelSpan() const
{
	const FDMXFixtureMode* ModePtr = GetActiveMode();

	if (ModePtr)
	{
		return ModePtr->ChannelSpan;
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
	/** DEPRECATED 4.27 */
	UE_LOG(LogDMXRuntime, Error, TEXT("No clear remote Universe can be deduced in DMXEntityFixturePatch::GetRemoteUniverse. Returning 0."));
	return 0;
}

TArray<FDMXAttributeName> UDMXEntityFixturePatch::GetAllAttributesInActiveMode() const
{
	TArray<FDMXAttributeName> NameArray;

	const FDMXFixtureMode* ModePtr = GetActiveMode();
	if (!ModePtr)
	{
		return NameArray;
	}

	const TArray<FDMXFixtureFunction>& Functions = ModePtr->Functions;
	NameArray.Reserve(Functions.Num());
	for (const FDMXFixtureFunction& Function : Functions)
	{
		NameArray.Add(Function.Attribute);
	}

	return NameArray;
}

TMap<FDMXAttributeName, FDMXFixtureFunction> UDMXEntityFixturePatch::GetAttributeFunctionsMap() const
{
	TMap<FDMXAttributeName, FDMXFixtureFunction> FunctionMap;

	const FDMXFixtureMode* ModePtr = GetActiveMode();
	if (!ModePtr)
	{
		return FunctionMap;
	}

	const TArray<FDMXFixtureFunction>& Functions = ModePtr->Functions;
	FunctionMap.Reserve(Functions.Num());
	for (const FDMXFixtureFunction& Function : Functions)
	{
		if (UDMXEntityFixtureType::GetFunctionLastChannel(Function) <= ModePtr->ChannelSpan)
		{
			FunctionMap.Add(Function.Attribute, Function);
		}
	}
	return FunctionMap;
}

TMap<FDMXAttributeName, int32> UDMXEntityFixturePatch::GetAttributeDefaultMap() const
{
	TMap<FDMXAttributeName, int32> DefaultValueMap;

	const FDMXFixtureMode* ModePtr = GetActiveMode();
	if (!ModePtr)
	{
		return DefaultValueMap;
	}

	const TArray<FDMXFixtureFunction>& Functions = ModePtr->Functions;
	for (const FDMXFixtureFunction& Function : Functions)
	{
		if (UDMXEntityFixtureType::GetFunctionLastChannel(Function) <= ModePtr->ChannelSpan)
		{
			DefaultValueMap.Add(Function.Attribute, Function.DefaultValue);
		}
	}
	
	return DefaultValueMap;
}

TMap<FDMXAttributeName, int32> UDMXEntityFixturePatch::GetAttributeChannelAssignments() const
{
	TMap<FDMXAttributeName, int32> ChannelMap;

	const FDMXFixtureMode* ModePtr = GetActiveMode();
	if (!ModePtr)
	{
		return ChannelMap;
	}

	const TArray<FDMXFixtureFunction>& Functions = ModePtr->Functions;
	ChannelMap.Reserve(Functions.Num());
	for (const FDMXFixtureFunction& Function : Functions)
	{
		ChannelMap.Add(Function.Attribute, Function.Channel + GetStartingChannel() - 1);
	}

	return ChannelMap;
}

TMap<FDMXAttributeName, EDMXFixtureSignalFormat> UDMXEntityFixturePatch::GetAttributeSignalFormats() const
{
	TMap<FDMXAttributeName, EDMXFixtureSignalFormat> FormatMap;

	const FDMXFixtureMode* ModePtr = GetActiveMode();
	if (!ModePtr)
	{
		return FormatMap;
	}

	const TArray<FDMXFixtureFunction>& Functions = ModePtr->Functions;
	FormatMap.Reserve(Functions.Num());
	for (const FDMXFixtureFunction& Function : Functions)
	{
		if (UDMXEntityFixtureType::GetFunctionLastChannel(Function) <= ModePtr->ChannelSpan)
		{
			FormatMap.Add(Function.Attribute, Function.DataType);
		}
	}
	return FormatMap;
}

TMap<FDMXAttributeName, int32> UDMXEntityFixturePatch::ConvertRawMapToAttributeMap(const TMap<int32, uint8>& RawMap) const
{
	TMap<FDMXAttributeName, int32> FunctionMap;

	const FDMXFixtureMode* ModePtr = GetActiveMode();
	if (!ModePtr)
	{
		return FunctionMap;
	}

	const TArray<FDMXFixtureFunction>& Functions = ModePtr->Functions;
	// Let's consider all functions in the raw map are 8bit and allocate for 1 channel = 1 function
	// We'll avoid many allocations but potentially have a map 4x the size we need. We can shrink it later
	FunctionMap.Reserve(RawMap.Num());

	for (const FDMXFixtureFunction& Function : Functions)
	{
		const int32 FunctionStartingChannel = Function.Channel + (GetStartingChannel() - 1);

		// Ignore functions outside the Active Mode's Channel Span
		if (UDMXEntityFixtureType::GetFunctionLastChannel(Function) <= ModePtr->ChannelSpan && 
			RawMap.Contains(FunctionStartingChannel))
		{
			const uint8& RawValue(RawMap.FindRef(Function.Channel));
			const uint8 ChannelsToAdd = UDMXEntityFixtureType::NumChannelsToOccupy(Function.DataType);

			TArray<uint8, TFixedAllocator<4>> Bytes;
			for (uint8 ChannelIt = 0; ChannelIt < ChannelsToAdd; ChannelIt++)
			{
				if (const uint8* RawVal = RawMap.Find(FunctionStartingChannel + ChannelIt))
				{
					Bytes.Add(*RawVal);
				}
			}

			const uint32 IntValue = UDMXEntityFixtureType::BytesToInt(Function.DataType, Function.bUseLSBMode, Bytes.GetData());

			FunctionMap.Add(Function.Attribute, IntValue);
		}
	}

	return FunctionMap;
}

TMap<int32, uint8> UDMXEntityFixturePatch::ConvertAttributeMapToRawMap(const TMap<FDMXAttributeName, int32>& FunctionMap) const
{
	TMap<int32, uint8> RawMap;

	const FDMXFixtureMode* ModePtr = GetActiveMode();
	if (!ModePtr)
	{
		return RawMap;
	}

	const TArray<FDMXFixtureFunction>& Functions = ModePtr->Functions;

	RawMap.Reserve(FunctionMap.Num());

	for (const TPair<FDMXAttributeName, int32>& Elem : FunctionMap)
	{
		// Search for a function with Attribute == Elem.Key.Attribute
		const FDMXFixtureFunction* FunctionPtr = Functions.FindByPredicate([&Elem](const FDMXFixtureFunction& Function) {
			return Elem.Key == Function.Attribute;
		});

		// Also check for the Function being in the valid range for the Active Mode's channel span
		if (FunctionPtr != nullptr && ModePtr->ChannelSpan >= UDMXEntityFixtureType::GetFunctionLastChannel(*FunctionPtr))
		{
			const int32 FunctionStartingChannel = FunctionPtr->Channel + (GetStartingChannel() - 1);
			const uint8 NumChannels = UDMXEntityFixtureType::NumChannelsToOccupy(FunctionPtr->DataType);
			const uint32 Value = UDMXEntityFixtureType::ClampValueToDataType(FunctionPtr->DataType, Elem.Value);

			// To avoid branching in the loop, we'll decide before it on which byte to start
			// and which direction to go, depending on the Function's bit endianness.
			const int8 ByteIndexStep = FunctionPtr->bUseLSBMode ? 1 : -1;
			int8 ByteIndex = FunctionPtr->bUseLSBMode ? 0 : NumChannels - 1;

			for (uint8 ByteOffset = 0; ByteOffset < NumChannels; ++ByteOffset)
			{
				const uint8 ChannelVal = (Value >> (8 * ByteOffset)) & 0xFF;

				const int32 FinalChannel = FunctionStartingChannel + ByteIndex;
				RawMap.Add(FinalChannel, ChannelVal);

				ByteIndex += ByteIndexStep;
			}
		}
	}

	RawMap.Shrink();
	return RawMap;
}

bool UDMXEntityFixturePatch::IsMapValid(const TMap<FDMXAttributeName, int32>& FunctionMap) const
{
	const FDMXFixtureMode* ModePtr = GetActiveMode();
	if (!ModePtr)
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

	const TArray<FDMXFixtureFunction>& Functions = ModePtr->Functions;

	for (const TPair<FDMXAttributeName, int32>& Elem : FunctionMap)
	{
		if (!ContainsAttribute(Elem.Key))
		{
			return false;
		}
	}
	return true;
}

bool UDMXEntityFixturePatch::ContainsAttribute(const FDMXAttributeName FunctionAttribute) const
{
	const FDMXFixtureMode* ModePtr = GetActiveMode();
	if (!ModePtr)
	{
		return false;
	}

	const TArray<FDMXFixtureFunction>& Functions = ModePtr->Functions;
	return Functions.ContainsByPredicate([&FunctionAttribute](const FDMXFixtureFunction& Function)
		{
			return FunctionAttribute == Function.Attribute;
		});
}

TMap<FDMXAttributeName, int32> UDMXEntityFixturePatch::ConvertToValidMap(const TMap<FDMXAttributeName, int32>& FunctionMap) const
{
	TMap<FDMXAttributeName, int32> ValidMap;

	const FDMXFixtureMode* ModePtr = GetActiveMode();
	if (!ModePtr)
	{
		return ValidMap;
	}

	const TArray<FDMXFixtureFunction>& Functions = ModePtr->Functions;
	ValidMap.Reserve(FunctionMap.Num());

	for (const TPair<FDMXAttributeName, int32>& Elem : FunctionMap)
	{
		// Search for a function with Attribute == Elem.Key.Attribute
		const FDMXFixtureFunction* FunctionPtr = Functions.FindByPredicate([&Elem](const FDMXFixtureFunction& Function)
		{
			return Elem.Key == Function.Attribute;
		});

		// Also check for the Function being in the valid range for the Active Mode's channel span
		if (FunctionPtr != nullptr && ModePtr->ChannelSpan >= UDMXEntityFixtureType::GetFunctionLastChannel(*FunctionPtr))
		{
			ValidMap.Add(Elem.Key, Elem.Value);
		}
	}

	ValidMap.Shrink();
	return ValidMap;
}

TArray<UDMXEntityController*> UDMXEntityFixturePatch::GetRelevantControllers() const
{
	// DEPRECATED 4.27
	TArray<UDMXEntityController*> EmptyArray;
	return EmptyArray;
}

const FDMXFixtureFunction* UDMXEntityFixturePatch::GetAttributeFunction(const FDMXAttributeName& Attribute) const
{
	const FDMXFixtureMode* ModePtr = GetActiveMode();
	if (!ModePtr)
	{
		UE_LOG(DMXEntityFixturePatchLog, Warning, TEXT("%S: Can't read the active Mode from the Fixture Type"), __FUNCTION__);
		return nullptr;
	}

	// No need for any search if there are no Functions on the active Mode.
	if (ModePtr->Functions.Num() == 0)
	{
		return nullptr;
	}

	const FDMXFixtureMode& Mode = ParentFixtureTypeTemplate->Modes[ActiveMode];
	const int32 FixtureChannelStart = GetStartingChannel() - 1;

	// Search the Function mapped to the selected Attribute
	for (const FDMXFixtureFunction& Function : ModePtr->Functions)
	{
		if (Function.Channel > DMX_MAX_ADDRESS)
		{
			// Following functions will have even higher Channels, so we can stop searching
			break;
		}

		if (!UDMXEntityFixtureType::IsFunctionInModeRange(Function, *ModePtr, FixtureChannelStart))
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

int32 UDMXEntityFixturePatch::GetAttributeValue(FDMXAttributeName Attribute, bool& bSuccess)
{
	// Update the cache if it isn't updated on tick
	if (!IsTickable())
	{
		UpdateCachedValues();
	}

	if (const FDMXFixtureFunction* FunctionPtr = GetAttributeFunction(Attribute))
	{
		const int32 FunctionStartIndex = FunctionPtr->Channel - 1;
		const int32 FunctionLastIndex = FunctionStartIndex + UDMXEntityFixtureType::NumChannelsToOccupy(FunctionPtr->DataType) - 1;
		if (FunctionLastIndex < CachedDMXValues.Num())
		{
			bSuccess = true;

			return UDMXEntityFixtureType::BytesToFunctionValue(*FunctionPtr, CachedDMXValues.GetData() + FunctionStartIndex);
		}
	}

	bSuccess = false;
	return 0;
}

float UDMXEntityFixturePatch::GetNormalizedAttributeValue(FDMXAttributeName Attribute, bool& bSuccess)
{
	// Update the cache if it isn't updated on tick
	if (!IsTickable())
	{
		UpdateCachedValues();
	}

	const float* ValuePtr = CachedNormalizedValuesPerAttribute.Map.Find(Attribute);
	if (ValuePtr)
	{
		bSuccess = true;
		return *ValuePtr;
	}
	bSuccess = false;
	return 0.0f;
}

void UDMXEntityFixturePatch::GetAttributesValues(TMap<FDMXAttributeName, int32>& AttributesValues)
{
	AttributesValues.Reset();

	const FDMXFixtureMode* ModePtr = GetActiveMode();
	if (ModePtr)
	{
		// Update the cache if it isn't updated on tick
		if (!IsTickable())
		{
			UpdateCachedValues();
		}

		const FDMXFixtureMode& Mode = ParentFixtureTypeTemplate->Modes[ActiveMode];

		for (const FDMXFixtureFunction& Function : ModePtr->Functions)
		{
			const int32 FunctionStartIndex = Function.Channel - 1;
			const int32 FunctionLastIndex = FunctionStartIndex + UDMXEntityFixtureType::NumChannelsToOccupy(Function.DataType) - 1;
			if (FunctionLastIndex >= CachedDMXValues.Num())
			{
				break;
			}

			const uint32 IntValue = UDMXEntityFixtureType::BytesToFunctionValue(Function, CachedDMXValues.GetData() + FunctionStartIndex);
			AttributesValues.Add(Function.Attribute, IntValue);
		}
	}
}

void UDMXEntityFixturePatch::GetNormalizedAttributesValues(FDMXNormalizedAttributeValueMap& NormalizedAttributesValues)
{
	// Update the cache if it isn't updated on tick
	if (!IsTickable())
	{
		UpdateCachedValues();
	}

	NormalizedAttributesValues = CachedNormalizedValuesPerAttribute;
}

bool UDMXEntityFixturePatch::SendMatrixCellValue(const FIntPoint& CellCoordinate, const FDMXAttributeName& Attribute, int32 Value)
{
	TMap<FDMXAttributeName, int32> AttributeNameChannelMap;
	GetMatrixCellChannelsAbsolute(CellCoordinate, AttributeNameChannelMap);

	return SendMatrixCellValueWithAttributeMap(CellCoordinate, Attribute, Value, AttributeNameChannelMap);
}

bool UDMXEntityFixturePatch::SendMatrixCellValueWithAttributeMap(const FIntPoint& CellCoordinate, const FDMXAttributeName& Attribute, int32 Value, const TMap<FDMXAttributeName, int32>& InAttributeNameChannelMap)
{
	const FDMXFixtureMatrix* const FixtureMatrixPtr = GetFixtureMatrixValidated();

	if (!FixtureMatrixPtr)
	{
		return false;
	}

	const FDMXFixtureMatrix& FixtureMatrix = *FixtureMatrixPtr;

	if (!AreCoordinatesValid(FixtureMatrix, CellCoordinate))
	{
		return false;
	}

	if (!ensure(InAttributeNameChannelMap.Num()))
	{
		return false;
	}

	TMap<int32, uint8> DMXChannelToValueMap;
	for (const FDMXFixtureCellAttribute& CellAttribute : FixtureMatrix.CellAttributes)
	{
		if (!InAttributeNameChannelMap.Contains(Attribute))
		{
			continue;
		}

		const int32 FirstChannel = InAttributeNameChannelMap[Attribute];
		const int32 LastChannel = FirstChannel + UDMXEntityFixtureType::NumChannelsToOccupy(CellAttribute.DataType) - 1;

		TArray<uint8> ByteArr;
		ByteArr.AddZeroed(4);
		UDMXEntityFixtureType::IntToBytes(CellAttribute.DataType, CellAttribute.bUseLSBMode, Value, ByteArr.GetData());

		int32 ByteOffset = 0;
		for (int32 Channel = FirstChannel; Channel <= LastChannel; Channel++)
		{
			DMXChannelToValueMap.Add(Channel, ByteArr[ByteOffset]);
			ByteOffset++;
		}
	}

	/** Send to the library's output ports */
	if (UDMXLibrary* DMXLibrary = ParentLibrary.Get())
	{
		for (const FDMXOutputPortSharedRef& OutputPort : DMXLibrary->GetOutputPorts())
		{
			OutputPort->SendDMX(UniverseID, DMXChannelToValueMap);
		}
	}

	return true;
}

bool UDMXEntityFixturePatch::SendNormalizedMatrixCellValue(const FIntPoint& CellCoordinate, const FDMXAttributeName& Attribute, float Value)
{
	const FDMXFixtureMatrix* FixtureMatrixPtr = GetFixtureMatrixValidated();

	if (!FixtureMatrixPtr)
	{
		return false;
	}

	const FDMXFixtureMatrix& FixtureMatrix = *FixtureMatrixPtr;

	const FDMXFixtureCellAttribute* const ExistingAttributePtr = FixtureMatrix.CellAttributes.FindByPredicate([&Attribute](const FDMXFixtureCellAttribute& TestedCellAttribute) {
		return TestedCellAttribute.Attribute == Attribute;
		});

	if (ExistingAttributePtr)
	{
		const FDMXFixtureCellAttribute& ExistingAttribute = *ExistingAttributePtr;

		Value = FMath::Clamp(Value, 0.0f, 1.0f);
		const uint32 IntValue = UDMXEntityFixtureType::GetDataTypeMaxValue(ExistingAttribute.DataType) * Value;

		SendMatrixCellValue(CellCoordinate, Attribute, IntValue);

		return true;
	}
	
	return false;
}

bool UDMXEntityFixturePatch::GetMatrixCellValues(const FIntPoint& CellCoordinate, TMap<FDMXAttributeName, int32>& ValuePerAttribute)
{
	ValuePerAttribute.Reset();

	// Update the cache if it isn't updated on tick
	if (!IsTickable())
	{
		UpdateCachedValues();
	}

	if (!LastDMXSignal.IsValid())
	{
		return false;
	}

	const FDMXFixtureMatrix* const FixtureMatrixPtr = GetFixtureMatrixValidated();

	if (!FixtureMatrixPtr)
	{
		return false;
	}

	const FDMXFixtureMatrix& FixtureMatrix = *FixtureMatrixPtr;

	if (!AreCoordinatesValid(FixtureMatrix, CellCoordinate))
	{
		return false;
	}

	TMap<FDMXAttributeName, int32> AttributeNameChannelMap;
	GetMatrixCellChannelsAbsolute(CellCoordinate, AttributeNameChannelMap);

	for (const FDMXFixtureCellAttribute& CellAttribute : FixtureMatrix.CellAttributes)
	{
		TArray<uint8> ChannelValues;
		for (const TPair<FDMXAttributeName, int32>& AttributeNameChannelKvp : AttributeNameChannelMap)
		{
			if (CellAttribute.Attribute != AttributeNameChannelKvp.Key)
			{
				continue;
			}

			int32 FirstChannel = AttributeNameChannelKvp.Value;
			int32 LastChannel = FirstChannel + UDMXEntityFixtureType::NumChannelsToOccupy(CellAttribute.DataType) - 1;

			for (int32 Channel = FirstChannel; Channel <= LastChannel; Channel++)
			{
				int32 ChannelIndex = Channel - 1;

				check(LastDMXSignal->ChannelData.IsValidIndex(ChannelIndex));
				ChannelValues.Add(LastDMXSignal->ChannelData[ChannelIndex]);
			}
		}

		const int32 Value = UDMXEntityFixtureType::BytesToInt(CellAttribute.DataType, CellAttribute.bUseLSBMode, ChannelValues.GetData());

		ValuePerAttribute.Add(CellAttribute.Attribute, Value);
	}

	return true;
}

bool UDMXEntityFixturePatch::GetNormalizedMatrixCellValues(const FIntPoint& CellCoordinate, TMap<FDMXAttributeName, float>& NormalizedValuePerAttribute)
{
	NormalizedValuePerAttribute.Reset();

	const FDMXFixtureMatrix* FixtureMatrixPtr = GetFixtureMatrixValidated();

	if (!FixtureMatrixPtr)
	{
		return false;
	}
	const FDMXFixtureMatrix& FixtureMatrix = *FixtureMatrixPtr;

	// Update the cache if it isn't updated on tick
	if (!IsTickable())
	{
		UpdateCachedValues();
	}

	TMap<FDMXAttributeName, int32> ValuePerAttribute;
	if (GetMatrixCellValues(CellCoordinate, ValuePerAttribute))
	{
		for (const TPair<FDMXAttributeName, int32>& AttributeValueKvp : ValuePerAttribute)
		{
			const FDMXAttributeName& AttributeName = AttributeValueKvp.Key;

			const FDMXFixtureCellAttribute* const ExistingAttributePtr = FixtureMatrix.CellAttributes.FindByPredicate([&AttributeName](const FDMXFixtureCellAttribute& TestedCellAttribute) {
				return TestedCellAttribute.Attribute == AttributeName;
				});

			if (ExistingAttributePtr)
			{
				const float NormalizedValue = (float)AttributeValueKvp.Value / (float)UDMXEntityFixtureType::GetDataTypeMaxValue(ExistingAttributePtr->DataType);

				NormalizedValuePerAttribute.Add(AttributeValueKvp.Key, NormalizedValue);
			}
		}

		return true;
	}

	return false;
}

bool UDMXEntityFixturePatch::GetMatrixCellChannelsRelative(const FIntPoint& CellCoordinate, TMap<FDMXAttributeName, int32>& AttributeChannelMap)
{
	const FDMXFixtureMatrix* const FixtureMatrixPtr = GetFixtureMatrixValidated();

	if (!FixtureMatrixPtr)
	{
		return false;
	}

	const FDMXFixtureMatrix& FixtureMatrix = *FixtureMatrixPtr;

	if (!AreCoordinatesValid(FixtureMatrix, CellCoordinate))
	{
		return false;
	}

	TArray<int32> Channels;
	for (const FDMXFixtureCellAttribute& CellAttribute : FixtureMatrix.CellAttributes)
	{
		if (!FixtureMatrix.GetChannelsFromCell(CellCoordinate, CellAttribute.Attribute, Channels))
		{
			continue;
		}

		check(Channels.IsValidIndex(0));
			
		if (Channels[0] > DMX_MAX_ADDRESS)
		{
			break;
		}

		AttributeChannelMap.Add(CellAttribute.Attribute, Channels[0]);
	}

	return true;
}

bool UDMXEntityFixturePatch::GetMatrixCellChannelsAbsolute(const FIntPoint& CellCoordinate /* Cell X/Y */, TMap<FDMXAttributeName, int32>& AttributeChannelMap)
{
	int32 PatchStartingChannelOffset = GetStartingChannel() - 1;
	if(GetMatrixCellChannelsRelative(CellCoordinate, AttributeChannelMap))
	{
		for (TPair<FDMXAttributeName, int32>& AttributeChannelKvp : AttributeChannelMap)
		{
			int32 Channel = AttributeChannelKvp.Value + PatchStartingChannelOffset;
			if (Channel > DMX_MAX_ADDRESS)
			{
				break;
			}

			AttributeChannelKvp.Value = Channel;
		}

		return true;
	}

	return false;
}

bool UDMXEntityFixturePatch::GetMatrixCellChannelsAbsoluteWithValidation(const FIntPoint& InCellCoordinate, TMap<FDMXAttributeName, int32>& OutAttributeChannelMap)
{
	const FDMXFixtureMatrix* const FixtureMatrixPtr = GetFixtureMatrixValidated();

	if (!FixtureMatrixPtr)
	{
		return false;
	}

	const FDMXFixtureMatrix& FixtureMatrix = *FixtureMatrixPtr;
	if (AreCoordinatesValid(FixtureMatrix, InCellCoordinate))
	{
		return GetMatrixCellChannelsAbsolute(InCellCoordinate, OutAttributeChannelMap);
	}

	return false;
}

bool UDMXEntityFixturePatch::GetMatrixProperties(FDMXFixtureMatrix& MatrixProperties) const
{
	const FDMXFixtureMatrix* FixtureMatrixPtr = GetFixtureMatrixValidated();

	if (FixtureMatrixPtr)
	{
		MatrixProperties = *FixtureMatrixPtr;
		return true;
	}

	return false;
}

bool UDMXEntityFixturePatch::GetCellAttributes(TArray<FDMXAttributeName>& CellAttributeNames)
{
	const FDMXFixtureMatrix* const FixtureMatrixPtr = GetFixtureMatrixValidated();

	if (!FixtureMatrixPtr)
	{
		return false;
	}

	const FDMXFixtureMatrix& FixtureMatrix = *FixtureMatrixPtr;

	for (const FDMXFixtureCellAttribute& CellAtrribute : FixtureMatrix.CellAttributes)
	{
		if (!CellAtrribute.Attribute.IsNone())
		{
			CellAttributeNames.Add(CellAtrribute.Attribute);
		}
			
	}
		
	return true;
}

bool UDMXEntityFixturePatch::GetMatrixCell(const FIntPoint& CellCoordinate, FDMXCell& OutCell)
{
	const FDMXFixtureMatrix* const FixtureMatrixPtr = GetFixtureMatrixValidated();

	if (!FixtureMatrixPtr)
	{
		return false;
	}

	const FDMXFixtureMatrix& FixtureMatrix = *FixtureMatrixPtr;

	TArray<int32> AllIDs;
	TArray<int32> OrderedIDs;

	int32 XCells = FixtureMatrix.XCells;
	int32 YCells = FixtureMatrix.YCells;

	for (int32 YCell = 0; YCell < YCells; YCell++)
	{
		for (int32 XCell = 0; XCell < XCells; XCell++)
		{
			AllIDs.Add(XCell + YCell * XCell);
		}
	}

	FDMXUtils::PixelMappingDistributionSort(FixtureMatrix.PixelMappingDistribution, XCells, YCells, AllIDs, OrderedIDs);

	FDMXCell Cell;
	Cell.Coordinate = CellCoordinate;
	Cell.CellID = OrderedIDs[CellCoordinate.Y + CellCoordinate.X * XCells] + 1;
		
	OutCell = Cell;

	return true;
}

bool UDMXEntityFixturePatch::GetAllMatrixCells(TArray<FDMXCell>& Cells)
{
	const FDMXFixtureMatrix* const FixtureMatrixPtr = GetFixtureMatrixValidated();

	if (!FixtureMatrixPtr)
	{
		return false;
	}

	const FDMXFixtureMatrix& FixtureMatrix = *FixtureMatrixPtr;

	TArray<int32> AllIDs;
	TArray<int32> OrderedIDs;

	int32 XCells = FixtureMatrix.XCells;
	int32 YCells = FixtureMatrix.YCells;

	for (int32 YCell = 0; YCell < YCells; YCell++)
	{
		for (int32 XCell = 0; XCell < XCells; XCell++)
		{
			AllIDs.Add(XCell + YCell * XCells);
		}
	}

	FDMXUtils::PixelMappingDistributionSort(FixtureMatrix.PixelMappingDistribution, XCells, YCells, AllIDs, OrderedIDs);

	for (int32 YCell = 0; YCell < YCells; YCell++)
	{
		for (int32 XCell = 0; XCell < XCells; XCell++)
		{

			FDMXCell Cell;

			Cell.Coordinate = FIntPoint(XCell, YCell);
			Cell.CellID = OrderedIDs[YCell + XCell * YCells] + 1;

			Cells.Add(Cell);
		}
	}

	return true;
}

const FDMXFixtureMatrix* UDMXEntityFixturePatch::GetFixtureMatrixValidated() const
{
	if (!ParentFixtureTypeTemplate)
	{
		UE_LOG(DMXEntityFixturePatchLog, Error, TEXT("Fixture Patch %s has no parent fixture type assigned"), *GetDisplayName());
		return nullptr;
	}

	if (!ParentFixtureTypeTemplate->bFixtureMatrixEnabled)
	{
		UE_LOG(DMXEntityFixturePatchLog, Error, TEXT("Fixture Patch %s is not a Matrix Fixture"), *GetDisplayName());
		return nullptr;
	}

	const FDMXFixtureMode* ModePtr = GetActiveMode();
	if (!ModePtr)
	{
		UE_LOG(DMXEntityFixturePatchLog, Error, TEXT("Invalid active Mode in Fixture Patch %s"), *GetDisplayName());
		return nullptr;
	}

	return &ModePtr->FixtureMatrixConfig;
}

bool UDMXEntityFixturePatch::AreCoordinatesValid(const FDMXFixtureMatrix& FixtureMatrix, const FIntPoint& Coordinate, bool bLogged)
{
	bool bValidX = Coordinate.X < FixtureMatrix.XCells && Coordinate.X >= 0;
	bool bValidY = Coordinate.Y < FixtureMatrix.YCells && Coordinate.Y >= 0;

	if (bValidX && bValidY)
	{
		return true;
	}

	if (bLogged)
	{
		UE_LOG(DMXEntityFixturePatchLog, Error, TEXT("Invalid X Coordinate for patch (requested %d, expected in range 0-%d)."), Coordinate.X, FixtureMatrix.XCells - 1);
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
