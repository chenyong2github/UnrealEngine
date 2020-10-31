// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXSubsystem.h"

#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "DMXProtocolTypes.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityReference.h"
#include "Library/DMXEntity.h"
#include "Library/DMXEntityController.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXEntityFixturePatch.h"

#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "DMXAttribute.h"
#include "DMXUtils.h"

const FName InvalidUniverseError = FName("InvalidUniverseError");

DECLARE_LOG_CATEGORY_CLASS(DMXSubsystemLog, Log, All);

void UDMXSubsystem::SendDMX(UDMXEntityFixturePatch* FixturePatch, TMap<FDMXAttributeName, int32> AttributeMap, EDMXSendResult& OutResult)
{
	OutResult = EDMXSendResult::ErrorSetBuffer;

	if (FixturePatch != nullptr)
	{
		IDMXFragmentMap DMXFragmentMap;
		for (const TPair<FDMXAttributeName, int32>& Elem : AttributeMap)
		{
			if (const UDMXEntityFixtureType* ParentType = FixturePatch->ParentFixtureTypeTemplate)
			{
				if (ParentType->Modes.Num() < 1)
				{
					UE_LOG(DMXSubsystemLog, Error, TEXT("%S: Tried to use Fixture Patch which Parent Fixture Type has no Modes set up."));
					return;
				}

				const int32 ActiveMode = FMath::Min(FixturePatch->ActiveMode, ParentType->Modes.Num() - 1);
				const FDMXFixtureMode& RelevantMode = ParentType->Modes[ActiveMode];
				for (const FDMXFixtureFunction& Function : RelevantMode.Functions)
				{
					const FDMXAttributeName FunctionAttr = Function.Attribute;
					if (FunctionAttr == Elem.Key)
					{
						if (!UDMXEntityFixtureType::IsFunctionInModeRange(Function, RelevantMode, FixturePatch->GetStartingChannel() - 1))
						{
							continue;
						}

						const int32 Channel = Function.Channel + FixturePatch->GetStartingChannel() - 1;

						uint32 ChannelValue = 0;
						uint8* ChannelValueBytes = reinterpret_cast<uint8*>(&ChannelValue);
						UDMXEntityFixtureType::FunctionValueToBytes(Function, Elem.Value, ChannelValueBytes);

						const uint8 NumBytesInSignalFormat = UDMXEntityFixtureType::NumChannelsToOccupy(Function.DataType);
						for (uint8 ChannelIt = 0; ChannelIt < NumBytesInSignalFormat; ++ChannelIt)
						{
							DMXFragmentMap.Add(Channel + ChannelIt, ChannelValueBytes[ChannelIt]);
						}
					}
				}
			}
		}

		if (FixturePatch->GetRelevantControllers().Num() > 0)
		{
			const int32& Universe = FixturePatch->UniverseID;
			const TArray<UDMXEntityController*>&& RelevantControllers = FixturePatch->GetRelevantControllers();
			TArray<EDMXSendResult> Results;
			TSet<uint32> UniversesUsed;

			Results.Reserve(RelevantControllers.Num());
			UniversesUsed.Reserve(RelevantControllers.Num());

			// Send using the Remote Offset from each Controller with this Fixture's Universe in its range
			for (const UDMXEntityController* Controller : RelevantControllers)
			{
				const uint32 RemoteUniverse = Universe + Controller->RemoteOffset;
				if (!UniversesUsed.Contains(RemoteUniverse))
				{
					const IDMXProtocolPtr Protocol = Controller->DeviceProtocol.GetProtocol();
					if (Protocol.IsValid())
					{
						bool bLoopback = !Protocol->IsReceiveDMXEnabled() || !Protocol->IsSendDMXEnabled();						
						if (bLoopback)
						{
							Results.Add(Protocol->InputDMXFragment(Universe + Controller->RemoteOffset, DMXFragmentMap));
						}
						
						Results.Add(Protocol->SendDMXFragment(Universe + Controller->RemoteOffset, DMXFragmentMap));
						UniversesUsed.Add(RemoteUniverse); // Avoid setting values in the same Universe more than once							
					}
				}
			}

			for (const EDMXSendResult& Result : Results)
			{
				if (Result != EDMXSendResult::Success)
				{
					OutResult = Result;
					return;
				}
			}

			OutResult = EDMXSendResult::Success;
		}
	}
}

bool UDMXSubsystem::SetMatrixCellValue(UDMXEntityFixturePatch* FixturePatch, FIntPoint Cell, FDMXAttributeName Attribute, int32 Value)
{
	if (FixturePatch)
	{
		return FixturePatch->SendMatrixCellValue(Cell, Attribute, Value);
	}

	return false;
}

bool UDMXSubsystem::GetMatrixCellValue(UDMXEntityFixturePatch* FixturePatch, FIntPoint Cells /* Cell X/Y */, TMap<FDMXAttributeName, int32>& AttributeValueMap)
{
	if (FixturePatch)
	{
		return FixturePatch->GetMatrixCellValues(Cells, AttributeValueMap);
	}

	return false;
}

bool UDMXSubsystem::GetMatrixCellChannelsRelative(UDMXEntityFixturePatch* FixturePatch, FIntPoint CellCoordinates /* Cell X/Y */, TMap<FDMXAttributeName, int32>& AttributeChannelMap)
{
	if (FixturePatch)
	{
		return FixturePatch->GetMatrixCellChannelsRelative(CellCoordinates, AttributeChannelMap);
	}

	return false;
}

bool UDMXSubsystem::GetMatrixCellChannelsAbsolute(UDMXEntityFixturePatch* FixturePatch, FIntPoint CellCoordinate /* Cell X/Y */, TMap<FDMXAttributeName, int32>& AttributeChannelMap)
{
	if (FixturePatch)
	{
		return FixturePatch->GetMatrixCellChannelsAbsolute(CellCoordinate, AttributeChannelMap);
	}

	return false;
}

bool UDMXSubsystem::GetMatrixProperties(UDMXEntityFixturePatch* FixturePatch, FDMXFixtureMatrix& MatrixProperties)
{
	if (FixturePatch)
	{
		return FixturePatch->GetMatrixProperties(MatrixProperties);
	}

	return false;
}

bool UDMXSubsystem::GetCellAttributes(UDMXEntityFixturePatch* FixturePatch, TArray<FDMXAttributeName>& CellAttributeNames)
{
	if (FixturePatch)
	{
		return FixturePatch->GetCellAttributes(CellAttributeNames);
	}

	return false;
}

bool UDMXSubsystem::GetMatrixCell(UDMXEntityFixturePatch* FixturePatch, FIntPoint Coordinate, FDMXCell& OutCell)
{
	if (FixturePatch)
	{
		return FixturePatch->GetMatrixCell(Coordinate, OutCell);
	}

	return false;
}

bool UDMXSubsystem::GetAllMatrixCells(UDMXEntityFixturePatch* FixturePatch, TArray<FDMXCell>& Cells)
{
	if (FixturePatch)
	{
		return FixturePatch->GetAllMatrixCells(Cells);
	}

	return false;
}

void UDMXSubsystem::PixelMappingDistributionSort(EDMXPixelMappingDistribution InDistribution, int32 InNumXPanels, int32 InNumYPanels, const TArray<int32>& InUnorderedList, TArray<int32>& OutSortedList)
{
	FDMXUtils::PixelMappingDistributionSort(InDistribution, InNumXPanels, InNumYPanels, InUnorderedList, OutSortedList);
}

void UDMXSubsystem::SendDMXRaw(FDMXProtocolName SelectedProtocol, int32 RemoteUniverse, TMap<int32, uint8> AddressValueMap, EDMXSendResult& OutResult)
{
	OutResult = EDMXSendResult::ErrorSetBuffer;

	if (RemoteUniverse < 0)
	{
		OutResult = EDMXSendResult::ErrorGetUniverse;
		FFrame::KismetExecutionMessage(TEXT("Invalid Universe Number: SendDMXRaw"), ELogVerbosity::Error, InvalidUniverseError);
		return;
	}

	if (SelectedProtocol)
	{
		IDMXFragmentMap DMXFragmentMap;
		for (auto& Elem : AddressValueMap)
		{
			if (Elem.Key != 0)
			{
				DMXFragmentMap.Add(Elem.Key, Elem.Value);
			}
		}
		IDMXProtocolPtr Protocol = SelectedProtocol.GetProtocol();
		if (Protocol.IsValid())
		{
			OutResult = Protocol->SendDMXFragmentCreate(RemoteUniverse, DMXFragmentMap);
		}
	}
}

void UDMXSubsystem::GetAllFixturesOfType(const FDMXEntityFixtureTypeRef& FixtureType, TArray<UDMXEntityFixturePatch*>& OutResult)
{
	OutResult.Reset();

	if (const UDMXEntityFixtureType* FixtureTypeObj = FixtureType.GetFixtureType())
	{
		FixtureTypeObj->GetParentLibrary()->ForEachEntityOfType<UDMXEntityFixturePatch>([&](UDMXEntityFixturePatch* Fixture)
		{
			if (Fixture->ParentFixtureTypeTemplate == FixtureTypeObj)
			{
				OutResult.Add(Fixture);
			}
		});
	}
}

void UDMXSubsystem::GetAllFixturesOfCategory(const UDMXLibrary* DMXLibrary, FDMXFixtureCategory Category, TArray<UDMXEntityFixturePatch*>& OutResult)
{
	OutResult.Reset();

	if (DMXLibrary != nullptr)
	{
		DMXLibrary->ForEachEntityOfType<UDMXEntityFixturePatch>([&](UDMXEntityFixturePatch* Fixture)
		{
			if (Fixture->ParentFixtureTypeTemplate != nullptr && Fixture->ParentFixtureTypeTemplate->DMXCategory == Category)
			{
				OutResult.Add(Fixture);
			}
		});
	}
}

void UDMXSubsystem::GetAllFixturesInUniverse(const UDMXLibrary* DMXLibrary, int32 UniverseId, TArray<UDMXEntityFixturePatch*>& OutResult)
{
	OutResult.Reset();

	if (DMXLibrary != nullptr)
	{
		DMXLibrary->ForEachEntityOfType<UDMXEntityFixturePatch>([&](UDMXEntityFixturePatch* Fixture)
		{
			if (Fixture->UniverseID == UniverseId)
			{
				OutResult.Add(Fixture);
			}
		});
	}
}

void UDMXSubsystem::GetAllUniversesInController(const UDMXLibrary* DMXLibrary, FString ControllerName, TArray<int32>& OutResult)
{
	OutResult.Reset();

	if (DMXLibrary != nullptr)
	{
		const UDMXEntityController* Controller = Cast<UDMXEntityController>(DMXLibrary->FindEntity(ControllerName));
		if (Controller != nullptr)
		{
			OutResult.Reserve(Controller->Endpoints.Num());
			const int32& RemoteOffset = Controller->RemoteOffset;

			// Get All Universes
			for (const FDMXCommunicationEndpoint& Endpoint : Controller->Endpoints)
			{
				// Remove remote offset to get local Universe IDs
				OutResult.Add(Endpoint.UniverseNumber - RemoteOffset);
			}
		}
	}
}

void UDMXSubsystem::GetRawBuffer(FDMXProtocolName SelectedProtocol, int32 RemoteUniverse, TArray<uint8>& DMXBuffer)
{
	DMXBuffer.Reset();
	if (SelectedProtocol)
	{
		IDMXProtocolPtr Protocol = SelectedProtocol.GetProtocol();
		if (Protocol.IsValid())
		{
			const IDMXUniverseSignalMap InboundSignalMap = Protocol->GameThreadGetInboundSignals();

			const TSharedPtr<FDMXSignal>* SignalPtr = InboundSignalMap.Find(RemoteUniverse);
			if (SignalPtr)
			{
				DMXBuffer = (*SignalPtr)->ChannelData;
			}
		}
	}
}

void UDMXSubsystem::GetFixtureAttributes(const UDMXEntityFixturePatch* InFixturePatch, const TArray<uint8>& DMXBuffer, TMap<FDMXAttributeName, int32>& OutResult)
{
	OutResult.Reset();

	if (InFixturePatch != nullptr)
	{
		if (const UDMXEntityFixtureType* FixtureType = InFixturePatch->ParentFixtureTypeTemplate)
		{
			const int32 StartingAddress = InFixturePatch->GetStartingChannel() - 1;

			if (FixtureType->Modes.Num() < 1)
			{
				UE_LOG(DMXSubsystemLog, Error, TEXT("%S: Tried to use Fixture Patch which Parent Fixture Type has no Modes set up."));
				return;
			}
			const int32 ActiveMode = FMath::Min(InFixturePatch->ActiveMode, FixtureType->Modes.Num() - 1);
			const FDMXFixtureMode& CurrentMode = FixtureType->Modes[ActiveMode];

			for (const FDMXFixtureFunction& Function : CurrentMode.Functions)
			{
				if (!UDMXEntityFixtureType::IsFunctionInModeRange(Function, CurrentMode, StartingAddress))
				{
					// This function and the following ones are outside the Universe's range.
					break;
				}

				const int32 ChannelIndex = Function.Channel - 1 + StartingAddress;
				if (ChannelIndex >= DMXBuffer.Num())
				{
					continue;
				}
				const uint32 ChannelVal = UDMXEntityFixtureType::BytesToFunctionValue(Function, DMXBuffer.GetData() + ChannelIndex);

				OutResult.Add(Function.Attribute, ChannelVal);
			}
		}
	}
}

UDMXEntityFixturePatch* UDMXSubsystem::GetFixturePatch(FDMXEntityFixturePatchRef InFixturePatch)
{
	return InFixturePatch.GetFixturePatch();
}

bool UDMXSubsystem::GetFunctionsMap(UDMXEntityFixturePatch* InFixturePatch, TMap<FDMXAttributeName, int32>& OutAttributesMap)
{
	OutAttributesMap.Empty();

	if (InFixturePatch == nullptr)
	{
		UE_LOG(DMXSubsystemLog, Warning, TEXT("%S: FixturePatch is null"), __FUNCTION__);

		return false;
	}

	UDMXEntityFixtureType* TypeTemplate = InFixturePatch->ParentFixtureTypeTemplate;
	if (TypeTemplate == nullptr)
	{
		UE_LOG(DMXSubsystemLog, Warning, TEXT("%S: InFixturePatch '%s' ParentFixtureTypeTemplate is null."), __FUNCTION__, *InFixturePatch->GetDisplayName());

		return false;
	}

	if (!InFixturePatch->CanReadActiveMode())
	{
		UE_LOG(DMXSubsystemLog, Warning, TEXT("Wrong ActiveMode %d, Num of modes %d"), InFixturePatch->ActiveMode, TypeTemplate->Modes.Num());

		return false;
	}

	if (!InFixturePatch->GetFirstRelevantController())
	{
		UE_LOG(DMXSubsystemLog, Warning, TEXT("%s has no controller to receive DMX from."), *InFixturePatch->GetDisplayName());

		return false;
	}

	FDMXProtocolName ProtocolName = InFixturePatch->GetFirstRelevantController()->DeviceProtocol;
	IDMXProtocolPtr Protocol = ProtocolName.GetProtocol();
	if (!Protocol.IsValid())
	{
		UE_LOG(DMXSubsystemLog, Warning, TEXT("%s has no valid protocol"), *InFixturePatch->GetDisplayName());

		return false;
	}

	const IDMXUniverseSignalMap& SingalMap = Protocol->GameThreadGetInboundSignals();
	int32 UniverseID = InFixturePatch->GetRemoteUniverse();
	
	const TSharedPtr<FDMXSignal>* SignalPtr = SingalMap.Find(UniverseID);

	if(SignalPtr)
	{ 
		const TArray<uint8>& ChannelData = (*SignalPtr)->ChannelData;
		
		const FDMXFixtureMode& Mode = TypeTemplate->Modes[InFixturePatch->ActiveMode];
		const int32 PatchStartingIndex = InFixturePatch->GetStartingChannel() - 1;

		for (const FDMXFixtureFunction& Function : Mode.Functions)
		{
			const int32 FunctionStartIndex = Function.Channel - 1 + PatchStartingIndex;
			const int32 FunctionLastIndex = FunctionStartIndex + UDMXEntityFixtureType::NumChannelsToOccupy(Function.DataType) - 1;
			if (FunctionLastIndex >= ChannelData.Num())
			{
				break;
			}

			const uint32 ChannelValue = UDMXEntityFixtureType::BytesToFunctionValue(Function, ChannelData.GetData() + FunctionStartIndex);
			OutAttributesMap.Add(Function.Attribute, ChannelValue);
		}
	}

	return true;
}

bool UDMXSubsystem::GetFunctionsMapForPatch(UDMXEntityFixturePatch* InFixturePatch, TMap<FDMXAttributeName, int32>& OutAttributesMap)
{
	TArray<UDMXEntityController*> Controllers = InFixturePatch->GetRelevantControllers();

	for(UDMXEntity* ControllerEntity : Controllers)
	{
		UDMXEntityController* Controller = Cast< UDMXEntityController>(ControllerEntity);
		bool success = GetFunctionsMap(InFixturePatch, OutAttributesMap);		
		return success;
	}

	return false;
}

int32 UDMXSubsystem::GetFunctionsValue(const FName FunctionAttributeName, const TMap<FDMXAttributeName, int32>& InAttributesMap)
{
	for (const TPair<FDMXAttributeName, int32>& kvp : InAttributesMap)
	{
		if(kvp.Key.Name.IsEqual(FunctionAttributeName))
		{
			const int32* Result = InAttributesMap.Find(kvp.Key);
			if (Result != nullptr)
			{
				return *Result;
			}
		}
	}	

	return 0;
}

bool UDMXSubsystem::PatchIsOfSelectedType(UDMXEntityFixturePatch* InFixturePatch, FString RefTypeValue)
{
	FDMXEntityFixtureTypeRef FixtureTypeRef;

	FDMXEntityReference::StaticStruct()
		->ImportText(*RefTypeValue, &FixtureTypeRef, nullptr, EPropertyPortFlags::PPF_None, GLog, FDMXEntityReference::StaticStruct()->GetName());

	if (FixtureTypeRef.DMXLibrary != nullptr)
	{
		UDMXEntityFixtureType* FixtureType = FixtureTypeRef.GetFixtureType();

		TArray<UDMXEntityFixturePatch*> AllPatchesOfType;
		GetAllFixturesOfType(FixtureType, AllPatchesOfType);

		if (AllPatchesOfType.Contains(InFixturePatch))
		{
			return true;
		}
	}

	return false;
}

FName UDMXSubsystem::GetAttributeLabel(FDMXAttributeName AttributeName)
{
	return AttributeName.Name;
}

/*static*/ UDMXSubsystem* UDMXSubsystem::GetDMXSubsystem_Pure()
{
	return GEngine->GetEngineSubsystem<UDMXSubsystem>();
}

/*static*/ UDMXSubsystem* UDMXSubsystem::GetDMXSubsystem_Callable()
{
	return UDMXSubsystem::GetDMXSubsystem_Pure();
}

TArray<UDMXEntityFixturePatch*> UDMXSubsystem::GetAllFixturesWithTag(const UDMXLibrary* DMXLibrary, FName CustomTag)
{
	TArray<UDMXEntityFixturePatch*> FoundPatches;

	if (DMXLibrary != nullptr)
	{
		DMXLibrary->ForEachEntityOfType<UDMXEntityFixturePatch>([&](UDMXEntityFixturePatch* Patch)
		{
			if (Patch->CustomTags.Contains(CustomTag))
			{
				FoundPatches.Add(Patch);
			}
		});
	}

	return FoundPatches;
}

TArray<UDMXEntityFixturePatch*> UDMXSubsystem::GetAllFixturesInLibrary(const UDMXLibrary* DMXLibrary)
{
	TArray<UDMXEntityFixturePatch*> FoundPatches;
	
	if (DMXLibrary != nullptr)
	{
		DMXLibrary->ForEachEntityOfType<UDMXEntityFixturePatch>([&](UDMXEntityFixturePatch* Patch)
		{
			FoundPatches.Add(Patch);
		});
	}

	// Sort patches by universes and channels
	FoundPatches.Sort([](const UDMXEntityFixturePatch& FixturePatchA, const UDMXEntityFixturePatch& FixturePatchB) {

		if (FixturePatchA.UniverseID < FixturePatchB.UniverseID)
		{
			return true;
		}

		bool bSameUniverse = FixturePatchA.UniverseID == FixturePatchB.UniverseID;
		if (bSameUniverse)
		{
			return FixturePatchA.GetStartingChannel() <= FixturePatchB.GetStartingChannel();
		}
	
		return false;
	});

	return FoundPatches;
}

template<class TEntityType>
TEntityType* GetDMXEntityByName(const UDMXLibrary* DMXLibrary, const FString& Name)
{
	if (DMXLibrary != nullptr)
	{
		TEntityType* FoundEntity = nullptr;
		DMXLibrary->ForEachEntityOfTypeWithBreak<TEntityType>([&](TEntityType* Entity)
		{
			if (Entity->Name.Equals(Name))
			{
				FoundEntity = Entity;
				return false;
			}
			return true;
		});

		return FoundEntity;
	}

	return nullptr;
}

UDMXEntityFixturePatch* UDMXSubsystem::GetFixtureByName(const UDMXLibrary* DMXLibrary, const FString& Name)
{
	return GetDMXEntityByName<UDMXEntityFixturePatch>(DMXLibrary, Name);
}

TArray<UDMXEntityFixtureType*> UDMXSubsystem::GetAllFixtureTypesInLibrary(const UDMXLibrary* DMXLibrary)
{
	TArray<UDMXEntityFixtureType*> FoundTypes;

	if (DMXLibrary != nullptr)
	{
		DMXLibrary->ForEachEntityOfType<UDMXEntityFixtureType>([&](UDMXEntityFixtureType* Type)
		{
			FoundTypes.Add(Type);
		});
	}

	return FoundTypes;
}

UDMXEntityFixtureType* UDMXSubsystem::GetFixtureTypeByName(const UDMXLibrary* DMXLibrary, const FString& Name)
{
	return GetDMXEntityByName<UDMXEntityFixtureType>(DMXLibrary, Name);
}

TArray<UDMXEntityController*> UDMXSubsystem::GetAllControllersInLibrary(const UDMXLibrary* DMXLibrary)
{
	TArray<UDMXEntityController*> FoundControllers;

	if (DMXLibrary != nullptr)
	{
		DMXLibrary->ForEachEntityOfType<UDMXEntityController>([&](UDMXEntityController* Controller)
		{
			FoundControllers.Add(Controller);
		});
	}

	return FoundControllers;
}

UDMXEntityController* UDMXSubsystem::GetControllerByName(const UDMXLibrary* DMXLibrary, const FString& Name)
{
	return GetDMXEntityByName<UDMXEntityController>(DMXLibrary, Name);
}

const TArray<UDMXLibrary*>& UDMXSubsystem::GetAllDMXLibraries()
{
	return LoadedDMXLibraries;
}

FORCEINLINE EDMXFixtureSignalFormat SignalFormatFromBytesNum(uint32 InBytesNum)
{
	switch (InBytesNum)
	{
	case 0:
		UE_LOG(DMXSubsystemLog, Error, TEXT("%S called with InBytesNum = 0"), __FUNCTION__);
		return EDMXFixtureSignalFormat::E8Bit;
	case 1:
		return EDMXFixtureSignalFormat::E8Bit;
	case 2:
		return EDMXFixtureSignalFormat::E16Bit;
	case 3:
		return EDMXFixtureSignalFormat::E24Bit;
	case 4:
		return EDMXFixtureSignalFormat::E32Bit;
	default: // InBytesNum is 4 or higher
		UE_LOG(DMXSubsystemLog, Warning, TEXT("%S called with InBytesNum > 4. Only 4 bytes will be used."), __FUNCTION__);
		return EDMXFixtureSignalFormat::E32Bit;
	}
}

int32 UDMXSubsystem::BytesToInt(const TArray<uint8>& Bytes, bool bUseLSB /*= false*/) const
{
	if (Bytes.Num() == 0)
	{
		return 0;
	}

	const EDMXFixtureSignalFormat SignalFormat = SignalFormatFromBytesNum(Bytes.Num());
	return UDMXEntityFixtureType::BytesToInt(SignalFormat, bUseLSB, Bytes.GetData());
}

float UDMXSubsystem::BytesToNormalizedValue(const TArray<uint8>& Bytes, bool bUseLSB /*= false*/) const
{
	if (Bytes.Num() == 0)
	{
		return 0;
	}

	const EDMXFixtureSignalFormat SignalFormat = SignalFormatFromBytesNum(Bytes.Num());
	return UDMXEntityFixtureType::BytesToNormalizedValue(SignalFormat, bUseLSB, Bytes.GetData());
}

void UDMXSubsystem::NormalizedValueToBytes(float InValue, EDMXFixtureSignalFormat InSignalFormat, TArray<uint8>& Bytes, bool bUseLSB /*= false*/) const
{
	const uint8 NumBytes = UDMXEntityFixtureType::NumChannelsToOccupy(InSignalFormat);
	// Make sure the array will fit the correct number of bytes
	Bytes.Reset(NumBytes);
	Bytes.AddZeroed(NumBytes);

	UDMXEntityFixtureType::NormalizedValueToBytes(InSignalFormat, bUseLSB, InValue, Bytes.GetData());
}

void UDMXSubsystem::IntValueToBytes(int32 InValue, EDMXFixtureSignalFormat InSignalFormat, TArray<uint8>& Bytes, bool bUseLSB /*= false*/)
{
	const uint8 NumBytes = UDMXEntityFixtureType::NumChannelsToOccupy(InSignalFormat);
	// Make sure the array will fit the correct number of bytes
	Bytes.Reset(NumBytes);
	Bytes.AddZeroed(NumBytes);

	UDMXEntityFixtureType::IntToBytes(InSignalFormat, bUseLSB, InValue, Bytes.GetData());
}

float UDMXSubsystem::IntToNormalizedValue(int32 InValue, EDMXFixtureSignalFormat InSignalFormat) const
{
	return (float)(uint32)(InValue) / UDMXEntityFixtureType::GetDataTypeMaxValue(InSignalFormat);
}

float UDMXSubsystem::GetNormalizedAttributeValue(UDMXEntityFixturePatch* InFixturePatch, FDMXAttributeName InFunctionAttribute, int32 InValue) const
{
	if (InFixturePatch == nullptr)
	{
		UE_LOG(DMXSubsystemLog, Error, TEXT("%S: InFixturePatch is null!"), __FUNCTION__);
		return 0.0f;
	}

	UDMXEntityFixtureType* ParentType = InFixturePatch->ParentFixtureTypeTemplate;
	if (ParentType == nullptr)
	{
		UE_LOG(DMXSubsystemLog, Error, TEXT("%S: InFixturePatch->ParentFixtureTypeTemplate is null!"), __FUNCTION__);
		return 0.0f;
	}

	if (ParentType->Modes.Num() == 0)
	{
		UE_LOG(DMXSubsystemLog, Error, TEXT("%S: InFixturePatch's Fixture Type has no Modes!"), __FUNCTION__);
		return 0.0f;
	}

	if (InFixturePatch->ActiveMode >= InFixturePatch->ParentFixtureTypeTemplate->Modes.Num())
	{
		UE_LOG(DMXSubsystemLog, Error, TEXT("%S: InFixturePatch' ActiveMode is not an existing mode from its Fixture Type!"), __FUNCTION__);
		return 0.0f;
	}

	const FDMXFixtureMode& Mode = ParentType->Modes[InFixturePatch->ActiveMode];

	// Search for a Function named InFunctionName in the Fixture Type current mode
	for (const FDMXFixtureFunction& Function : Mode.Functions)
	{
		if (Function.Attribute == InFunctionAttribute)
		{
			return IntToNormalizedValue(InValue, Function.DataType);
		}
	}

	return -1.0f;
}

void UDMXSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(FName("AssetRegistry")).Get();
	AssetRegistry.OnFilesLoaded().AddUObject(this, &UDMXSubsystem::OnAssetRegistryFinishedLoadingFiles);
	AssetRegistry.OnAssetAdded().AddUObject(this, &UDMXSubsystem::OnAssetRegistryAddedAsset);
	AssetRegistry.OnAssetRemoved().AddUObject(this, &UDMXSubsystem::OnAssetRegistryRemovedAsset);

	// Register delegates
	TArray<FName> ProtocolNames = IDMXProtocol::GetProtocolNames();
	for (const FName& ProtocolName : ProtocolNames)
	{
		IDMXProtocolPtr Protocol = IDMXProtocol::Get(ProtocolName);
		check(Protocol.IsValid());

		Protocol->GetOnGameThreadOnlyBufferUpdated().AddUObject(this, &UDMXSubsystem::OnGameThreadOnlyBufferUpdated);
	}
}

void UDMXSubsystem::Deinitialize()
{

}

void UDMXSubsystem::OnAssetRegistryFinishedLoadingFiles()
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(FName("AssetRegistry")).Get();

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByClass(UDMXLibrary::StaticClass()->GetFName(), Assets, true);

	for (const FAssetData& Asset : Assets)
	{
		UObject* AssetObject = Asset.GetAsset();
		UDMXLibrary* Library = Cast<UDMXLibrary>(AssetObject);
		LoadedDMXLibraries.AddUnique(Library);
	}
}

void UDMXSubsystem::OnAssetRegistryAddedAsset(const FAssetData& Asset)
{
	if (Asset.AssetClass == UDMXLibrary::StaticClass()->GetFName())
	{
		UObject* AssetObject = Asset.GetAsset();
		UDMXLibrary* Library = Cast<UDMXLibrary>(AssetObject);
		LoadedDMXLibraries.AddUnique(Library);
	}
}

void UDMXSubsystem::OnAssetRegistryRemovedAsset(const FAssetData& Asset)
{
	if (Asset.AssetClass == UDMXLibrary::StaticClass()->GetFName())
	{
		UObject* AssetObject = Asset.GetAsset();
		UDMXLibrary* Library = Cast<UDMXLibrary>(AssetObject);
		LoadedDMXLibraries.Remove(Library);
	}
}

void UDMXSubsystem::OnGameThreadOnlyBufferUpdated(const FName& InProtocolName, int32 InUniverseID)
{
	if (IDMXProtocolPtr DMXProtocolPtr = IDMXProtocol::Get(InProtocolName))
	{
		const IDMXUniverseSignalMap& InboundSignalMap = DMXProtocolPtr->GameThreadGetInboundSignals();

		const TSharedPtr<FDMXSignal>* DMXSignalPtr = InboundSignalMap.Find(InUniverseID);
		if (DMXSignalPtr != nullptr)
		{
			const TSharedPtr<FDMXSignal>& DMXSignal = *DMXSignalPtr;
			const TArray<uint8>& DMXBuffer = DMXSignal.Get()->ChannelData;
			OnProtocolReceived.Broadcast(FDMXProtocolName(InProtocolName), InUniverseID, DMXBuffer);
		}
	}
}
