// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlProtocolMIDI.h"

#include "MIDIDeviceManager.h"
#include "RemoteControlProtocolMIDIModule.h"
#include "RemoteControlProtocolMIDISettings.h"

#define LOCTEXT_NAMESPACE "FRemoteControlProtocolMIDI"

int32 FRemoteControlMIDIDevice::ResolveDeviceId(const TArray<FFoundMIDIDevice>& InFoundDevices)
{
	if(InFoundDevices.Num() == 0)
	{
		// This is only displayed because it's valid and a common use case that bindings would be setup but unused by this particular project participant 
		UE_LOG(LogRemoteControlProtocolMIDI, Display, TEXT("RemoteControlProtocolMIDI bindings specified, but no devices are available."))
		return ResolvedDeviceId = INDEX_NONE;
	}
	
	// Call project settings if they're used
	if(DeviceSelector == ERemoteControlMIDIDeviceSelector::ProjectSettings)
	{
		URemoteControlProtocolMIDISettings* MIDISettings = GetMutableDefault<URemoteControlProtocolMIDISettings>();
		// Check that this isn't called from itself
		if(!ensureMsgf(MIDISettings->DefaultDevice != *this, TEXT("DeviceSelector == ERemoteControlMIDIDeviceSelector::ProjectSettings on RemoteControlProtocolMIDISettings.DefaultDevice! This is not valid.")))
		{
			return ResolvedDeviceId = INDEX_NONE;
		}

		return ResolvedDeviceId = MIDISettings->DefaultDevice.ResolveDeviceId(InFoundDevices);
	}

	// reset flag
	bDeviceIsAvailable = true;
	int32 FoundDeviceId = INDEX_NONE;
	// Convert for comparison
	const FString DeviceNameStr = DeviceName.ToString();
	for(const FFoundMIDIDevice& FoundDevice : InFoundDevices)
	{
		if(DeviceSelector == ERemoteControlMIDIDeviceSelector::DeviceId && DeviceId >= 0)
		{
			// User specified device id was found and valid, just return it
			if(FoundDevice.DeviceID == DeviceId)
			{
				if(FoundDevice.bIsAlreadyInUse)
				{
					bDeviceIsAvailable = false;
					FoundDeviceId = FoundDevice.DeviceID;
					break;
				}
				else
				{
					return ResolvedDeviceId = FoundDevice.DeviceID;	
				}
			}
		}

		if(DeviceSelector == ERemoteControlMIDIDeviceSelector::DeviceName && DeviceName != NAME_None)
		{
			if(FoundDevice.DeviceName.Equals(DeviceNameStr, ESearchCase::IgnoreCase))
			{
				// If the device is found, reset this DeviceId to the one found
				return ResolvedDeviceId = FoundDevice.DeviceID;
			}
		}
	}

	if(!bDeviceIsAvailable)
	{
		UE_LOG(LogRemoteControlProtocolMIDI, Warning, TEXT("MIDI Device with Id %i was found, but already in use and unavailable."), FoundDeviceId);
		bDeviceIsAvailable = false;
		// Set the correct device id (so it's valid), but it won't be used.
		return ResolvedDeviceId = FoundDeviceId;
	}

	// If we're here, no matching device was found
	if(DeviceSelector == ERemoteControlMIDIDeviceSelector::DeviceId)
	{
		UE_LOG(LogRemoteControlProtocolMIDI, Warning, TEXT("MIDI Device with user specified Id %i was not found, using Device 1."), DeviceId);
		// We've already checked if theres one or more devices
		return ResolvedDeviceId = InFoundDevices[0].DeviceID;
	}
	else
	{
		UE_LOG(LogRemoteControlProtocolMIDI, Warning, TEXT("MIDI Device with specified name %s was not found, using Device 1."), *DeviceName.ToString());
		// We've already checked if theres one or more devices
		return ResolvedDeviceId = InFoundDevices[0].DeviceID;
	}
}

void FRemoteControlMIDIDevice::SetDevice(const int32 InDeviceId, const FName& InDeviceName)
{
	DeviceSelector = ERemoteControlMIDIDeviceSelector::DeviceName;

	// Set's resolved id, not device id as that's for manual user specification
	ResolvedDeviceId = InDeviceId;
	DeviceName = InDeviceName;
}

void FRemoteControlMIDIDevice::SetUseProjectSettings()
{
	DeviceSelector = ERemoteControlMIDIDeviceSelector::ProjectSettings;
}

void FRemoteControlMIDIDevice::SetUserDeviceId()
{
	DeviceSelector = ERemoteControlMIDIDeviceSelector::DeviceId;
}

FText FRemoteControlMIDIDevice::ToDisplayName() const
{
	return FText::Format(LOCTEXT("DeviceMenuItem", "[{0}]: {1}"), FText::AsNumber(ResolvedDeviceId), FText::FromName(DeviceName));
}

void FRemoteControlProtocolMIDI::Bind(FRemoteControlProtocolEntityPtr InRemoteControlProtocolEntityPtr)
{
	if (!ensure(InRemoteControlProtocolEntityPtr.IsValid()))
	{
		return;
	}

	FRemoteControlMIDIProtocolEntity* MIDIProtocolEntity = InRemoteControlProtocolEntityPtr->CastChecked<FRemoteControlMIDIProtocolEntity>();

	TArray<FFoundMIDIDevice> Devices;
	UMIDIDeviceManager::FindMIDIDevices(Devices);
	MIDIProtocolEntity->Device.ResolveDeviceId(Devices);
	
	const int32 MIDIDeviceId = MIDIProtocolEntity->Device.ResolvedDeviceId;
	ensureMsgf(MIDIDeviceId >= 0, TEXT("MIDI Resolved Device Id was -1, ensure ResolveDeviceId is called first!"));

	TStrongObjectPtr<UMIDIDeviceInputController>* MIDIDeviceInputControllerPtr = MIDIDevices.Find(MIDIDeviceId);
	if (MIDIDeviceInputControllerPtr == nullptr)
	{
		TStrongObjectPtr<UMIDIDeviceInputController> MIDIDeviceInputController = TStrongObjectPtr<UMIDIDeviceInputController>(UMIDIDeviceManager::CreateMIDIDeviceInputController(MIDIDeviceId));
		if (!MIDIDeviceInputController.IsValid())
		{
			// Impossible to create a midi input controller
			return;
		}
		MIDIDeviceInputControllerPtr = &MIDIDeviceInputController;

		MIDIDeviceInputController->OnMIDIRawEvent.AddSP(this, &FRemoteControlProtocolMIDI::OnReceiveEvent);
		MIDIDevices.Add(MIDIDeviceId, MIDIDeviceInputController);
	}

	TMap<int32, TArray<FRemoteControlProtocolEntityWeakPtr>>* EntityBindingsMapPtr = MIDIDeviceBindings.Find(MIDIDeviceInputControllerPtr->Get());
	if (EntityBindingsMapPtr == nullptr)
	{
		EntityBindingsMapPtr = &MIDIDeviceBindings.Add(MIDIDeviceInputControllerPtr->Get());
	}

	if (TArray<FRemoteControlProtocolEntityWeakPtr>* ProtocolEntityBindingsArrayPtr = EntityBindingsMapPtr->Find(MIDIProtocolEntity->MessageData1))
	{
		ProtocolEntityBindingsArrayPtr->Emplace(MoveTemp(InRemoteControlProtocolEntityPtr));
	}
	else
	{
		TArray<FRemoteControlProtocolEntityWeakPtr> NewEntityBindingsArrayPtr { MoveTemp(InRemoteControlProtocolEntityPtr) };

		EntityBindingsMapPtr->Add(MIDIProtocolEntity->MessageData1, MoveTemp(NewEntityBindingsArrayPtr));
	}
}

void FRemoteControlProtocolMIDI::Unbind(FRemoteControlProtocolEntityPtr InRemoteControlProtocolEntityPtr)
{
	if (!ensure(InRemoteControlProtocolEntityPtr.IsValid()))
	{
		return;
	}

	FRemoteControlMIDIProtocolEntity* MIDIProtocolEntity = InRemoteControlProtocolEntityPtr->CastChecked<FRemoteControlMIDIProtocolEntity>();
	for (TPair<UMIDIDeviceInputController*, TMap<int32, TArray<FRemoteControlProtocolEntityWeakPtr>>>& BindingsPair : MIDIDeviceBindings)
	{
		TMap<int32, TArray<FRemoteControlProtocolEntityWeakPtr>>& BindingMap = BindingsPair.Value;

		for (TPair<int32, TArray<FRemoteControlProtocolEntityWeakPtr>>& BindingMapPair : BindingMap)
		{
			BindingMapPair.Value.RemoveAllSwap(CreateProtocolComparator(MIDIProtocolEntity->GetPropertyId()));
		}
	}
}

void FRemoteControlProtocolMIDI::OnReceiveEvent(UMIDIDeviceInputController* MIDIDeviceController, int32 Timestamp, int32 Type, int32 Channel, int32 MessageData1, int32 MessageData2)
{
	if (const TMap<int32, TArray<FRemoteControlProtocolEntityWeakPtr>>* MIDIMapBindingsPtr = MIDIDeviceBindings.Find(MIDIDeviceController))
	{
		if (const TArray<FRemoteControlProtocolEntityWeakPtr>* ProtocolEntityArrayPtr = MIDIMapBindingsPtr->Find(MessageData1))
		{
			for (const FRemoteControlProtocolEntityWeakPtr& ProtocolEntityWeakPtr : *ProtocolEntityArrayPtr)
			{
				if (const TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>> ProtocolEntityPtr = ProtocolEntityWeakPtr.Pin())
				{
					const FRemoteControlMIDIProtocolEntity* MIDIProtocolEntity = ProtocolEntityPtr->CastChecked<FRemoteControlMIDIProtocolEntity>();

					if (Type != MIDIProtocolEntity->EventType || Channel != MIDIProtocolEntity->Channel)
					{
						continue;
					}

					MIDIProtocolEntity->ApplyProtocolValueToProperty(MessageData2);
				}
			}
		}
	}
}

void FRemoteControlProtocolMIDI::UnbindAll()
{
	MIDIDeviceBindings.Empty();
	MIDIDevices.Empty();
}

#undef LOCTEXT_NAMESPACE
