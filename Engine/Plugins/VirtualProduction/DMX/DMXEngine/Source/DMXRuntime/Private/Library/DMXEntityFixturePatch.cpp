// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/DMXEntityFixturePatch.h"

#include "DMXProtocolConstants.h"
#include "DMXStats.h"
#include "DMXTypes.h"
#include "DMXUtils.h"
#include "Interfaces/IDMXProtocol.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityController.h"
#include "Library/DMXEntityFixtureType.h"

DECLARE_LOG_CATEGORY_CLASS(DMXEntityFixturePatchLog, Log, All);

DECLARE_CYCLE_STAT(TEXT("FixturePatch cache DMX values"), STAT_DMXFixturePatchCacheDMXValues, STATGROUP_DMX);
DECLARE_CYCLE_STAT(TEXT("FixturePatch cache normalized values"), STAT_DMXFixturePatchCacheNormalizedValues, STATGROUP_DMX);
DECLARE_CYCLE_STAT(TEXT("FixturePatch get relevant Controllers"), STAT_DMXFixturePatchGetRelevantControllers, STATGROUP_DMX);

#define LOCTEXT_NAMESPACE "DMXEntityFixturePatch"

UDMXEntityFixturePatch::UDMXEntityFixturePatch()
	: UniverseID(1)
	, bAutoAssignAddress(true)
	, ManualStartingAddress(1)
	, AutoStartingAddress(1)
	, ActiveMode(0)
{
#if WITH_EDITOR
	bTickInEditor = false;
#endif

	CachedDMXValues.Reserve(DMX_UNIVERSE_SIZE);
}

void UDMXEntityFixturePatch::Tick(float DeltaTime)
{
	// Note: This directly inherits TickableGameObject, Super::Tick wouldn't make sense here. 

	if (UpdateCachedDMXValues())
	{
		// Only update cached normalized values if cached dmx values changed
		UpdateCachedNormalizedAttributeValues();

		// Only broadcast changed values
		OnFixturePatchReceivedDMX.Broadcast(this, CachedNormalizedValuesPerAttribute);
	}
}

bool UDMXEntityFixturePatch::UpdateCachedDMXValues()
{
	SCOPE_CYCLE_COUNTER(STAT_DMXFixturePatchCacheDMXValues);

	// CachedRelevantController only exists to improve performance, profiler showed significant benefits.
	// Since the controller may change in editor, in such builds we need to test each tick for relevant ones.
	// We generally assume in the plugin that no controllers can be added or removed at runtime.
	// We still need to layout the API so that it prevents from runtime changes of controllers -> @TODO.
	//
	// This should help understand code below

#if WITH_EDITOR
	CachedRelevantController = nullptr;
	for (UDMXEntityController* Controller : GetRelevantControllers())
	{
		// If several controllers are sending we let the user know of the conflict, but do not try to merge the signals.
		if (CachedRelevantController)
		{
			UE_LOG(DMXEntityFixturePatchLog, Warning, TEXT("More than one controller sending data to %s. Using one arbitrarily, may differ for each run."), *GetDisplayName());
			break;
		}
		CachedRelevantController = Controller;
	}
#endif

	// Non-Editor builds cache the controller in the first call. Controllers aren't assumed to be changed at runtime
#if !WITH_EDITOR
	if (!CachedRelevantController)
	{
		CachedRelevantController = GetFirstRelevantController();
	}
#endif // !WITH_EDITOR


	bool bValuesChanged = false;
	if (CachedRelevantController)
	{
		const FName& Protocol = CachedRelevantController->GetProtocol();
		if (IDMXProtocolPtr DMXProtocolPtr = IDMXProtocol::Get(Protocol))
		{
			const IDMXUniverseSignalMap& InboundSignalMap = DMXProtocolPtr->GameThreadGetInboundSignals();
			const int32 FixturePatchRemoteUniverse = CachedRelevantController->RemoteOffset + UniverseID;

			if (InboundSignalMap.Contains(FixturePatchRemoteUniverse))
			{
				const TSharedPtr<FDMXSignal>& Signal = InboundSignalMap[FixturePatchRemoteUniverse];
				CachedLastDMXSignal = Signal;

				const int32 StartingIndex = GetStartingChannel() - 1;
				const int32 NumChannels = GetChannelSpan();

				// In cases where the num channel changes, update the Cache size. @TODO: Presumably can be editor only
				if (NumChannels != CachedDMXValues.Num())
				{
					CachedDMXValues.SetNum(NumChannels, false);
				}

				// Copy data relevant to the patch, to compare it with existing
				TArray<uint8> NewValuesArray(&Signal->ChannelData[StartingIndex], NumChannels);

				// Update only if values changed
				if (NewValuesArray != CachedDMXValues)
				{
					// Move the new values. Profiler shows benefits of MoveTemp in debug and development builds
					CachedDMXValues = MoveTemp(NewValuesArray);

					bValuesChanged = true;
				}
			}
		}
	}

	return bValuesChanged;
}

void UDMXEntityFixturePatch::UpdateCachedNormalizedAttributeValues()
{
	SCOPE_CYCLE_COUNTER(STAT_DMXFixturePatchCacheNormalizedValues);

	CachedNormalizedValuesPerAttribute.Map.Reset();

	if (ParentFixtureTypeTemplate && CanReadActiveMode())
	{
		const FDMXFixtureMode& Mode = ParentFixtureTypeTemplate->Modes[ActiveMode];

		for (const FDMXFixtureFunction& Function : Mode.Functions)
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
	}
}

TStatId UDMXEntityFixturePatch::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDMXPixelMappingBaseComponent, STATGROUP_Tickables);
}

bool UDMXEntityFixturePatch::IsTickableInEditor() const
{
#if WITH_EDITOR
	return bTickInEditor && !GIsPlayInEditorWorld;
#endif // WITH_EDITOR

#if !WITH_EDITOR
	return false;
#endif //!WITH_EDITOR
}

bool UDMXEntityFixturePatch::IsTickableWhenPaused() const
{
	return false;
}

bool UDMXEntityFixturePatch::IsTickable() const
{
	return OnFixturePatchReceivedDMX.IsBound();
}

int32 UDMXEntityFixturePatch::GetChannelSpan() const
{
	if (ParentFixtureTypeTemplate && CanReadActiveMode())
	{
		// Number of channels occupied by all of the Active Mode's functions
		return ParentFixtureTypeTemplate->Modes[ActiveMode].ChannelSpan;
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
	if (UDMXEntityController* RelevantController = GetFirstRelevantController())
	{
		return RelevantController->RemoteOffset + UniverseID;
	}
	return -1;
}

TArray<FName> UDMXEntityFixturePatch::GetAllFunctionsInActiveMode() const
{
	// DEPRECATED 4.26, FString conversion to FName is lossy
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
		NameArray.Add(Function.Attribute);
	}

	return NameArray;
}

TMap<FName, FDMXAttributeName> UDMXEntityFixturePatch::GetFunctionAttributesMap() const
{
	// DEPRECATED 4.26, FString conversion to FName is lossy
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
		const FDMXFixtureFunction* FunctionPtr = Functions.FindByPredicate([&Elem](const FDMXFixtureFunction& Function) {
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

const FDMXFixtureFunction* UDMXEntityFixturePatch::GetAttributeFunction(const FDMXAttributeName& Attribute) const
{
	if (!CanReadActiveMode())
	{
		UE_LOG(DMXEntityFixturePatchLog, Warning, TEXT("%S: Can't read the active Mode from the Fixture Type"), __FUNCTION__);
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
#if !WITH_EDITOR
	// Non-Editor builds return the cached relevant controller, if it exists
	if (CachedRelevantController)
	{
		return CachedRelevantController;
	}
#endif // !WITH_EDITOR

	// Parent library may be null if the patch was deleted from the library but is still referenced elsewhere
	if (ParentLibrary.IsValid())
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

	return nullptr;
}

TArray<UDMXEntityController*> UDMXEntityFixturePatch::GetRelevantControllers() const
{
	SCOPE_CYCLE_COUNTER(STAT_DMXFixturePatchGetRelevantControllers);

#if !WITH_EDITOR
	// Non-Editor builds return the cached relevant controller only, if it exists
	if (CachedRelevantController)
	{
		return TArray<UDMXEntityController*>({ CachedRelevantController });
	}
#endif // !WITH_EDITOR

	TArray<UDMXEntityController*> RetVal;
	if (ParentLibrary.IsValid())
	{
		for (UDMXEntity* Entity : ParentLibrary->GetEntities())
		{
			if (UDMXEntityController* Controller = Cast<UDMXEntityController>(Entity))
			{
				if (IsInControllerRange(Controller))
				{
					RetVal.Add(Controller);
				}
			}
		}
	}
	else
	{
		UE_LOG(DMXEntityFixturePatchLog, Fatal, TEXT("Parent library is null!"));
	}

	return MoveTemp(RetVal);
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

	if (ParentFixtureTypeTemplate && CanReadActiveMode())
	{
		const FDMXFixtureMode& Mode = ParentFixtureTypeTemplate->Modes[ActiveMode];

		for (const FDMXFixtureFunction& Function : Mode.Functions)
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
	NormalizedAttributesValues = CachedNormalizedValuesPerAttribute;
}

bool UDMXEntityFixturePatch::SendMatrixCellValue(const FIntPoint& CellCoordinate, const FDMXAttributeName& Attribute, int32 Value)
{
	const FDMXFixtureMatrix* const FixtureMatrixPtr = AccessFixtureMatrixValidated();

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

	IDMXFragmentMap DMXFragmentMap;
	for (const FDMXFixtureCellAttribute& CellAttribute : FixtureMatrix.CellAttributes)
	{
		if (!AttributeNameChannelMap.Contains(Attribute))
		{
			continue;
		}

		int32 FirstChannel = AttributeNameChannelMap[Attribute];
		int32 LastChannel = FirstChannel + UDMXEntityFixtureType::NumChannelsToOccupy(CellAttribute.DataType) - 1;

		TArray<uint8> ByteArr;
		ByteArr.AddZeroed(4);
		UDMXEntityFixtureType::IntToBytes(CellAttribute.DataType, CellAttribute.bUseLSBMode, Value, ByteArr.GetData());

		int32 ByteOffset = 0;
		for (int32 Channel = FirstChannel; Channel <= LastChannel; Channel++)
		{
			DMXFragmentMap.Add(Channel, ByteArr[ByteOffset]);
			ByteOffset++;
		}

		TArray<UDMXEntityController*> RelevantControllers = GetRelevantControllers();
		TArray<EDMXSendResult> Results;
		TSet<uint32> UniversesUsed;

		Results.Reserve(RelevantControllers.Num());
		UniversesUsed.Reserve(RelevantControllers.Num());

		// Send using the Remote Offset from each Controller with this Fixture's Universe in its range
		for (const UDMXEntityController* Controller : RelevantControllers)
		{
			const uint32 RemoteUniverse = UniverseID + Controller->RemoteOffset;
			if (!UniversesUsed.Contains(RemoteUniverse))
			{
				IDMXProtocolPtr Protocol = Controller->DeviceProtocol.GetProtocol();
				if (Protocol.IsValid())
				{
					// If sent DMX will not be looped back via network, input it directly
					bool bCanLoopback = Protocol->IsReceiveDMXEnabled() && Protocol->IsSendDMXEnabled();
					if (!bCanLoopback)
					{
						Protocol->InputDMXFragment(RemoteUniverse, DMXFragmentMap);
					}

					Results.Add(Protocol->SendDMXFragment(RemoteUniverse, DMXFragmentMap));

					UniversesUsed.Add(RemoteUniverse); // Avoid setting values in the same Universe more than once
				}
			}
		}

		for (const EDMXSendResult& Result : Results)
		{
			if (Result != EDMXSendResult::Success)
			{
				UE_LOG(DMXEntityFixturePatchLog, Error, TEXT("Error while sending DMX Packet"));
				return false;
			}
		}
	}

	return true;
}

bool UDMXEntityFixturePatch::SendNormalizedMatrixCellValue(const FIntPoint& CellCoordinate, const FDMXAttributeName& Attribute, float Value)
{
	const FDMXFixtureMatrix* FixtureMatrixPtr = AccessFixtureMatrixValidated();

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

	const FDMXFixtureMatrix* const FixtureMatrixPtr = AccessFixtureMatrixValidated();

	if (!FixtureMatrixPtr)
	{
		return false;
	}

	const FDMXFixtureMatrix& FixtureMatrix = *FixtureMatrixPtr;

	if (!AreCoordinatesValid(FixtureMatrix, CellCoordinate))
	{
		return false;
	}

	const TArray<UDMXEntityController*>&& RelevantControllers = GetRelevantControllers();

	TMap<FDMXAttributeName, int32> AttributeNameChannelMap;
	GetMatrixCellChannelsAbsolute(CellCoordinate, AttributeNameChannelMap);

	for (const UDMXEntityController* Controller : RelevantControllers)
	{
		IDMXProtocolPtr Protocol = Controller->DeviceProtocol.GetProtocol();
		TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> ProtocolUniverse = Protocol->GetUniverseById(UniverseID);
		check(ProtocolUniverse.IsValid());

		const IDMXUniverseSignalMap& InboundSignalMap = Protocol->GameThreadGetInboundSignals();
		const int32 FixturePatchRemoteUniverse = GetRemoteUniverse();

		if (InboundSignalMap.Contains(FixturePatchRemoteUniverse))
		{
			const TSharedPtr<FDMXSignal>& Signal = InboundSignalMap[FixturePatchRemoteUniverse];

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

						check(Signal->ChannelData.IsValidIndex(ChannelIndex));
						ChannelValues.Add(Signal->ChannelData[ChannelIndex]);
					}
				}

				const int32 Value = UDMXEntityFixtureType::BytesToInt(CellAttribute.DataType, CellAttribute.bUseLSBMode, ChannelValues.GetData());

				ValuePerAttribute.Add(CellAttribute.Attribute, Value);
			}
		}
	}

	return true;
}

bool UDMXEntityFixturePatch::GetNormalizedMatrixCellValues(const FIntPoint& CellCoordinate, TMap<FDMXAttributeName, float>& NormalizedValuePerAttribute)
{
	NormalizedValuePerAttribute.Reset();

	const FDMXFixtureMatrix* FixtureMatrixPtr = AccessFixtureMatrixValidated();

	if (!FixtureMatrixPtr)
	{
		return false;
	}
	const FDMXFixtureMatrix& FixtureMatrix = *FixtureMatrixPtr;

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
	}

	return true;
}

bool UDMXEntityFixturePatch::GetMatrixCellChannelsRelative(const FIntPoint& CellCoordinate, TMap<FDMXAttributeName, int32>& AttributeChannelMap)
{
	const FDMXFixtureMatrix* const FixtureMatrixPtr = AccessFixtureMatrixValidated();

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

bool UDMXEntityFixturePatch::GetMatrixProperties(FDMXFixtureMatrix& MatrixProperties)
{
	FDMXFixtureMatrix* FixtureMatrixPtr = AccessFixtureMatrixValidated();

	if (FixtureMatrixPtr)
	{
		MatrixProperties = *FixtureMatrixPtr;
		return true;
	}

	return false;
}

bool UDMXEntityFixturePatch::GetCellAttributes(TArray<FDMXAttributeName>& CellAttributeNames)
{
	const FDMXFixtureMatrix* const FixtureMatrixPtr = AccessFixtureMatrixValidated();

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
	const FDMXFixtureMatrix* const FixtureMatrixPtr = AccessFixtureMatrixValidated();

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
	const FDMXFixtureMatrix* const FixtureMatrixPtr = AccessFixtureMatrixValidated();

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

FDMXFixtureMatrix* UDMXEntityFixturePatch::AccessFixtureMatrixValidated()
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

	if (CanReadActiveMode() == false)
	{
		UE_LOG(DMXEntityFixturePatchLog, Error, TEXT("Invalid active Mode in Fixture Patch %s"), *GetDisplayName());
		return nullptr;
	}

	FDMXFixtureMode& RelevantMode = ParentFixtureTypeTemplate->Modes[ActiveMode];

	return &RelevantMode.FixtureMatrixConfig;
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
