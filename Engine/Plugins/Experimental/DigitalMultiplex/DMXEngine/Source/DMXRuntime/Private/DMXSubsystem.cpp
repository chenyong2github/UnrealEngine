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

#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "Async/Async.h"
#include "Engine/Engine.h"

DECLARE_LOG_CATEGORY_CLASS(DMXSubsystemLog, Log, All);

void UDMXSubsystem::SendDMX(FDMXProtocolName SelectedProtocol, UDMXEntityFixturePatch* FixturePatch, TMap<FName, int32> FunctionMap, EDMXSendResult& OutResult)
{
	OutResult = EDMXSendResult::ErrorSetBuffer;

	if (FixturePatch != nullptr)
	{
		IDMXFragmentMap DMXFragmentMap;
		for (const TPair<FName, int32>& Elem : FunctionMap)
		{
			if (const UDMXEntityFixtureType* ParentType = FixturePatch->ParentFixtureTypeTemplate)
			{
				if (ParentType->Modes.Num() < 1)
				{
					UE_LOG(DMXSubsystemLog, Error, TEXT("%S: Tried to use Fixture Patch which Parent Fixture Type has no Modes set up."));
					return;
				}

				const int32& ActiveMode = FMath::Min(FixturePatch->ActiveMode, ParentType->Modes.Num() - 1);
				const FDMXFixtureMode& RelevantMode = ParentType->Modes[ActiveMode];
				for (const FDMXFixtureFunction& Function : RelevantMode.Functions)
				{
					const FName FunctionName(*Function.FunctionName);
					if (FunctionName == Elem.Key)
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

		if (SelectedProtocol)
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
					IDMXProtocolPtr Protocol = SelectedProtocol.GetProtocol();
					if (Protocol.IsValid())
					{
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

void UDMXSubsystem::SendDMXRaw(FDMXProtocolName SelectedProtocol, int32 UniverseIndex, TMap<int32, uint8> ChannelValuesMap, EDMXSendResult& OutResult)
{
	OutResult = EDMXSendResult::ErrorSetBuffer;

	if (SelectedProtocol)
	{
		IDMXFragmentMap DMXFragmentMap;
		for (auto& Elem : ChannelValuesMap)
		{
			if (Elem.Key != 0)
			{
				DMXFragmentMap.Add(Elem.Key, Elem.Value);
			}
		}
		IDMXProtocolPtr Protocol = SelectedProtocol.GetProtocol();
		if (Protocol.IsValid())
		{
			OutResult = Protocol->SendDMXFragmentCreate(UniverseIndex, DMXFragmentMap);
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
			OutResult.Reserve(Controller->Universes.Num());
			const int32& RemoteOffset = Controller->RemoteOffset;

			// Get All Universes
			for (const FDMXUniverse& Universe : Controller->Universes)
			{
				// Remove remote offset to get local Universe IDs
				OutResult.Add(Universe.UniverseNumber - RemoteOffset);
			}
		}
	}
}

void UDMXSubsystem::GetRawBuffer(FDMXProtocolName SelectedProtocol, int32 UniverseIndex, TArray<uint8>& DMXBuffer)
{
	DMXBuffer.Reset();
	if (SelectedProtocol)
	{
		IDMXProtocolPtr Protocol = SelectedProtocol.GetProtocol();
		if (Protocol.IsValid())
		{
			TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> ProtocolUniverse = Protocol->GetUniverseById(UniverseIndex);
			if (ProtocolUniverse.IsValid())
			{
				FDMXBufferPtr Buffer = ProtocolUniverse.Get()->GetInputDMXBuffer();
				if (Buffer.IsValid())
				{
					Buffer->AccessDMXData([&DMXBuffer](TArray<uint8>& InData)
						{
							DMXBuffer = InData;
						});
				}
			}
		}
	}
}

void UDMXSubsystem::GetFixtureFunctions(const UDMXEntityFixturePatch* InFixturePatch, const TArray<uint8>& DMXBuffer, TMap<FName, int32>& OutResult)
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
			const int32& ActiveMode = FMath::Min(InFixturePatch->ActiveMode, FixtureType->Modes.Num() - 1);
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

				OutResult.Add(FName(*Function.FunctionName), ChannelVal);
			}
		}
	}
}

UDMXEntityFixturePatch* UDMXSubsystem::GetFixturePatch(FDMXEntityFixturePatchRef InFixturePatch)
{
	return InFixturePatch.GetFixturePatch();
}

bool UDMXSubsystem::GetFunctionsMap(UDMXEntityFixturePatch* InFixturePatch, const FDMXProtocolName& SelectedProtocol, TMap<FName, int32>& OutFunctionsMap)
{
	OutFunctionsMap.Empty();

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

	if (TypeTemplate->Modes.Num() < InFixturePatch->ActiveMode + 1)
	{
		UE_LOG(DMXSubsystemLog, Warning, TEXT("Wrong ActiveMode %d, Num of modes %d"), InFixturePatch->ActiveMode, TypeTemplate->Modes.Num());

		return false;
	}

	IDMXProtocolPtr Protocol = SelectedProtocol.GetProtocol();
	if (!Protocol.IsValid())
	{
		UE_LOG(DMXSubsystemLog, Warning, TEXT("Protocol Not Valid"));

		return false;
	}

	// Search for a controller that is assigned to the selected protocol and matches the Fixture Patch Universe ID
	const UDMXEntityController* SelectedController = nullptr;
	for (const UDMXEntityController* Controller : InFixturePatch->GetRelevantControllers())
	{
		if (Controller->DeviceProtocol == SelectedProtocol)
		{
			SelectedController = Controller;
			break;
		}
	}

	if (SelectedController == nullptr)
	{
		UE_LOG(DMXSubsystemLog, Warning, TEXT("%S: The Fixture Patch '%s' is not assigned to any existing Controller's Universe under the '%s' protocol."), __FUNCTION__, *InFixturePatch->GetDisplayName(), *SelectedProtocol.Name.ToString());

		return false;
	}

	TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> Universe = Protocol->GetUniverseById(InFixturePatch->UniverseID + SelectedController->RemoteOffset);
	if (!Universe.IsValid())
	{
		UE_LOG(DMXSubsystemLog, Warning, TEXT("Universe Not Valid"));

		return false;
	}

	FDMXBufferPtr InputDMXBuffer = Universe->GetInputDMXBuffer();
	if (!InputDMXBuffer.IsValid())
	{
		UE_LOG(DMXSubsystemLog, Warning, TEXT("InputDMXBuffer Not Valid"));

		return false;
	}

	const FDMXFixtureMode& Mode = TypeTemplate->Modes[InFixturePatch->ActiveMode];
	const int32 FixtureChannelStart = InFixturePatch->GetStartingChannel() - 1;

	for (const FDMXFixtureFunction& Function : Mode.Functions)
	{
		if (Function.Channel > DMX_MAX_ADDRESS)
		{
			UE_LOG(DMXSubsystemLog, Warning, TEXT("%S: Function Channel %d is higher than %d"), Function.Channel, DMX_MAX_ADDRESS);

			return false;
		}

		if (!UDMXEntityFixtureType::IsFunctionInModeRange(Function, Mode, FixtureChannelStart))
		{
			// We reached the functions outside the valid channels for this mode
			break;
		}

		InputDMXBuffer->AccessDMXData([&](TArray<uint8>& DMXData)
			{
				const int32 FunctionStartIndex = Function.Channel - 1 + FixtureChannelStart;
				const uint8 FunctionLastIndex = FunctionStartIndex + UDMXEntityFixtureType::NumChannelsToOccupy(Function.DataType) - 1;
				if (FunctionLastIndex >= DMXData.Num())
				{
					return;
				}

				const uint32 ChannelValue = UDMXEntityFixtureType::BytesToFunctionValue(Function, DMXData.GetData() + FunctionStartIndex);
				OutFunctionsMap.Add(*Function.FunctionName, ChannelValue);
			});
	}

	return true;
}

int32 UDMXSubsystem::GetFunctionsValue(const FName& InName, const TMap<FName, int32>& InFunctionsMap)
{
	const int32* Result = InFunctionsMap.Find(InName);
	if (Result != nullptr)
	{
		return *Result;
	}

	return 0;
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

TArray<UDMXLibrary*> UDMXSubsystem::GetAllDMXLibraries()
{
	TArray<UDMXLibrary*> LibrariesFound;

	for (TObjectIterator<UDMXLibrary> LibraryItr; LibraryItr; ++LibraryItr)
	{
		LibrariesFound.Add(*LibraryItr);
	}

	return LibrariesFound;
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

void UDMXSubsystem::IntValueToBytes(int32 InValue, EDMXFixtureSignalFormat InSignalFormat, TArray<uint8>& Bytes, bool bUseLSB /*= false*/) const
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

float UDMXSubsystem::GetNormalizedFunctionValue(UDMXEntityFixturePatch* InFixturePatch, FName InFunctionName, int32 InValue) const
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
		if (Function.FunctionName.Equals(InFunctionName.ToString()))
		{
			return IntToNormalizedValue(InValue, Function.DataType);
		}
	}

	return -1.0f;
}

void UDMXSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	for (const FName& ProtocolName : IDMXProtocol::GetProtocolNames())
	{
		if (IDMXProtocolPtr Protocol = IDMXProtocol::Get(ProtocolName))
		{
			// Using AddLambda instead of AddUObject because the UObject reference is stored
			// as a TWeakObjectPtr, which gets Garbage Collected when outside PIE and would break
			// Utility Blueprints that rely on this event and any subsequent PIE sections.
			FDelegateHandle UniverseUpdateHandle = Protocol->GetOnUniverseInputUpdate().AddLambda([](FName InProtocol, uint16 InUniverseID, const TArray<uint8>& InValues)
				{
					// Call Broadcast on GameThread to make sure a listener BP won't change
					// an Actor's properties outside of it
					AsyncTask(ENamedThreads::GameThread, [InProtocol, InUniverseID, InValues]()
						{
							// If this gets called after FEngineLoop::Exit(), GetEngineSubsystem() can crash
							if (IsEngineExitRequested())
							{
								return;
							}

							// The subsystem could be invalid by the time this code gets called
							UDMXSubsystem* DMXSubsystem = GEngine->GetEngineSubsystem<UDMXSubsystem>();
							if (DMXSubsystem != nullptr && DMXSubsystem->IsValidLowLevelFast())
							{
								DMXSubsystem->OnProtocolReceived.Broadcast(FDMXProtocolName(InProtocol), InUniverseID, InValues);
							}
						});
				});

			// Store handles to unbind from the event when this subsystem deinitializes
			UniverseInputUpdateHandles.Add(ProtocolName, UniverseUpdateHandle);
		}
	}
}

void UDMXSubsystem::Deinitialize()
{
	// Unbind from the protocols' universe update event
	for (const TPair<FName, FDelegateHandle>& It : UniverseInputUpdateHandles)
	{
		if (IDMXProtocolPtr Protocol = IDMXProtocol::Get(It.Key))
		{
			Protocol->GetOnUniverseInputUpdate().Remove(It.Value);
		}
	}
}
