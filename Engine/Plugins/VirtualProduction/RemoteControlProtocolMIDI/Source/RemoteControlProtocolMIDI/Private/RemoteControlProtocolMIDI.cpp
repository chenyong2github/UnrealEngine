// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlProtocolMIDI.h"

#include "MIDIDeviceManager.h"


void FRemoteControlProtocolMIDI::Bind(FRemoteControlProtocolEntityPtr InRemoteControlProtocolEntityPtr)
{
	if (!ensure(InRemoteControlProtocolEntityPtr.IsValid()))
	{
		return;
	}

	FRemoteControlMIDIProtocolEntity* MIDIProtocolEntity = InRemoteControlProtocolEntityPtr->CastChecked<FRemoteControlMIDIProtocolEntity>();

	TStrongObjectPtr<UMIDIDeviceInputController>* MIDIDeviceInputControllerPtr = MIDIDevices.Find(MIDIProtocolEntity->DeviceId);
	if (MIDIDeviceInputControllerPtr == nullptr)
	{
		TStrongObjectPtr<UMIDIDeviceInputController> MIDIDeviceInputController = TStrongObjectPtr<UMIDIDeviceInputController>(UMIDIDeviceManager::CreateMIDIDeviceInputController(MIDIProtocolEntity->DeviceId));
		if (!MIDIDeviceInputController.IsValid())
		{
			// Impossible to create a midi input controller
			return;
		}
		MIDIDeviceInputControllerPtr = &MIDIDeviceInputController;

		MIDIDeviceInputController->OnMIDIRawEvent.AddSP(this, &FRemoteControlProtocolMIDI::OnReceiveEvent);
		MIDIDevices.Add(MIDIProtocolEntity->DeviceId, MIDIDeviceInputController);
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