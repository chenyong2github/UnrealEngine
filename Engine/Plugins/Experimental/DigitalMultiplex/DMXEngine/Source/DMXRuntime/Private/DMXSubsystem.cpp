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

void UDMXSubsystem::SendDMX(FDMXProtocolName SelectedProtocol, UDMXEntityFixturePatch* FixturePatch, TMap<FName, int32> FunctionMap, EDMXSendResult& OutResult)
{
	OutResult = EDMXSendResult::ErrorSetBuffer;

	if (FixturePatch != nullptr)
	{
		IDMXFragmentMap DMXFragmentMap;
		for (const TPair<FName, int32>& Elem : FunctionMap)
		{
			const int32& ActiveMode = FixturePatch->ActiveMode;
			const FDMXFixtureMode& RelevantMode = FixturePatch->ParentFixtureTypeTemplate->Modes[ActiveMode];
			for (const FDMXFixtureFunction& Function : RelevantMode.Functions)
			{
				const FName FunctionName(*Function.FunctionName);
				if (FunctionName == Elem.Key && Function.Channel > 0)
				{
					const uint8 ChannelsToAdd = UDMXEntityFixtureType::NumChannelsToOccupy(Function.DataType);
					const int32& Channel = Function.Channel + (FixturePatch->GetStartingChannel() - 1);
					const uint32 FinalValue = UDMXEntityFixtureType::ClampValueToDataType(Function.DataType, Elem.Value);

					for (uint8 ChannelIt = 0; ChannelIt < ChannelsToAdd; ChannelIt++)
					{
						const uint8 BytesOffset = (ChannelIt) * 8;
						const uint8 RetVal = FinalValue >> BytesOffset & 0xff;

						DMXFragmentMap.Add(Channel + ChannelIt, RetVal);
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
					Results.Add(SelectedProtocol.GetProtocol()->SendDMXFragment(Universe + Controller->RemoteOffset, DMXFragmentMap));
					UniversesUsed.Add(RemoteUniverse); // Avoid setting values in the same Universe more than once
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
		OutResult = SelectedProtocol.GetProtocol()->SendDMXFragment(UniverseIndex, DMXFragmentMap);
	}
}

void UDMXSubsystem::GetAllFixturesOfType(const UDMXLibrary* DMXLibrary, const FName& FixtureType, TArray<UDMXEntityFixturePatch*>& OutResult)
{
	OutResult.Reset();

	if (DMXLibrary != nullptr)
	{
		DMXLibrary->ForEachEntityOfType<UDMXEntityFixturePatch>([&](UDMXEntityFixturePatch* Fixture)
			{
				if (Fixture->ParentFixtureTypeTemplate != nullptr && Fixture->ParentFixtureTypeTemplate->GetDisplayName() == FixtureType.ToString())
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
		TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> ProtocolUniverse = SelectedProtocol.GetProtocol()->GetUniverseById(UniverseIndex);
		if (ProtocolUniverse.IsValid())
		{
			TSharedPtr<FDMXBuffer> Buffer = ProtocolUniverse.Get()->GetInputDMXBuffer();
			if (Buffer.IsValid())
			{
				DMXBuffer = Buffer->GetDMXData();
			}
		}
	}
}

void UDMXSubsystem::GetFixtureFunctions(const UDMXEntityFixturePatch* InFixturePatch, const TArray<uint8>& DMXBuffer, TMap<FName, int32>& OutResult)
{
	OutResult.Reset();

	if (InFixturePatch != nullptr)
	{
		const int32& ActiveMode = InFixturePatch->ActiveMode;
		const int32 StartingAddress = InFixturePatch->GetStartingChannel();

		const UDMXEntityFixtureType* FixtureType = InFixturePatch->ParentFixtureTypeTemplate;
		if (FixtureType != nullptr)
		{
			const FDMXFixtureMode& CurrentMode = FixtureType->Modes[ActiveMode];

			for (const FDMXFixtureFunction& Function : CurrentMode.Functions)
			{
				const int32 IndexValue = (Function.Channel - 1) + (StartingAddress - 1);

				const uint8 ChannelsToAdd = UDMXEntityFixtureType::NumChannelsToOccupy(Function.DataType);

				TArray<uint8> Bytes;
				for (uint8 ChannelIt = 0; ChannelIt < ChannelsToAdd; ChannelIt++)
				{
					const uint8& ByteValue = DMXBuffer[IndexValue + ChannelIt];
					Bytes.Add(ByteValue);
				}

				uint32 IntVal = 0;
				for (uint8 ByteIndex = 0; ByteIndex < Bytes.Num() && ByteIndex < 4; ++ByteIndex)
				{
					IntVal += Bytes[ByteIndex] << (ByteIndex * 8);
				}
				OutResult.Add(FName(*Function.FunctionName), IntVal);
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
		UE_LOG(LogTemp, Warning, TEXT("No FixturePatch"));

		return false;
	}

	TSharedPtr<IDMXProtocol> Protocol = SelectedProtocol.GetProtocol();
	if (!Protocol.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Protocol Not Valid"));

		return false;
	}

	TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> Universe = Protocol->GetUniverseById(InFixturePatch->UniverseID);
	if (!Universe.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Universe Not Valid"));

		return false;
	}

	TSharedPtr<FDMXBuffer> InputDMXBuffer = Universe->GetInputDMXBuffer();
	if (!InputDMXBuffer.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("InputDMXBuffer Not Valid"));

		return false;
	}

	if (InFixturePatch == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("InFixturePatch == nullptr"));
		return false;
	}

	UDMXEntityFixtureType* TypeTemplate = InFixturePatch->ParentFixtureTypeTemplate;
	if (TypeTemplate == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("TypeTemplate == nullptr"));

		return false;
	}

	if (TypeTemplate->Modes.Num() < InFixturePatch->ActiveMode + 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("Wrong ActiveMode %d, Num of modes %d"), InFixturePatch->ActiveMode, TypeTemplate->Modes.Num());

		return false;
	}

	// Read the correct amount of channels for each function and store each in the correct byte of an int32
	const FDMXFixtureMode& Mode = TypeTemplate->Modes[InFixturePatch->ActiveMode];
	const TArray<uint8>& DMXData = InputDMXBuffer->GetDMXData();
	
	// Unsigned to make sure there won't be any system that changes how the bytes sum is done with int32
	uint32 ChannelValue = 0;

	for (const FDMXFixtureFunction& Function : Mode.Functions)
	{
		if (Function.Channel > DMX_MAX_ADDRESS)
		{
			UE_LOG(LogTemp, Warning, TEXT("Channel %d is more then %d"), Function.Channel, DMX_MAX_ADDRESS);

			return false;
		}

		if (UDMXEntityFixtureType::GetFunctionLastChannel(Function) > Mode.ChannelSpan)
		{
			// We reached the functions outside the valid channels for this mode
			break;
		}

		// Always reset all bytes to zero on our int, in case the new ones don't use all bytes.
		// Otherwise, the bytes not used now could have values from the previous function's channels.
		ChannelValue = 0;

		const int32 NumChannelsToRead = UDMXEntityFixtureType::NumChannelsToOccupy(Function.DataType);
		for (int32 ChannelIndex = 0; ChannelIndex < NumChannelsToRead && ChannelIndex + Function.Channel < DMXData.Num() && ChannelIndex < 4; ++ChannelIndex)
		{
			ChannelValue += DMXData[Function.Channel + ChannelIndex] << ChannelIndex * 8;
		}

		OutFunctionsMap.Add(*Function.FunctionName, ChannelValue);
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

int32 UDMXSubsystem::BytesToInt(const TArray<uint8>& Bytes)
{
	uint32 IntVal = 0;
	for (uint8 ByteIndex = 0; ByteIndex < Bytes.Num() && ByteIndex < 4; ++ByteIndex)
	{
		IntVal += Bytes[ByteIndex] << (ByteIndex * 8);
	}
	return IntVal;
}

void UDMXSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	for (const FName& ProtocolName : IDMXProtocol::GetProtocolNames())
	{
		if (TSharedPtr<IDMXProtocol> Protocol = IDMXProtocol::Get(ProtocolName))
		{
			Protocol->GetOnUniverseInputUpdate().AddUObject(this, &UDMXSubsystem::BufferReceivedBroadcast);
		}
	}
}

void UDMXSubsystem::BufferReceivedBroadcast(FName Protocol, uint16 UniverseID, const TArray<uint8>& Values)
{
	OnProtocolReceived.Broadcast(FDMXProtocolName(Protocol), UniverseID, Values);
}
