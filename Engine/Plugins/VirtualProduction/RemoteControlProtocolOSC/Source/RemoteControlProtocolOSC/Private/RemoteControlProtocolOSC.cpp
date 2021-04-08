// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlProtocolOSC.h"

#include "OSCMessage.h"
#include "OSCManager.h"

const FName FRemoteControlProtocolOSC::ProtocolName = TEXT("OSC");

void FRemoteControlProtocolOSC::Bind(FRemoteControlProtocolEntityPtr InRemoteControlProtocolEntityPtr)
{
	if (!ensure(InRemoteControlProtocolEntityPtr.IsValid()))
	{
		return;
	}

	FRemoteControlOSCProtocolEntity* OSCProtocolEntity = InRemoteControlProtocolEntityPtr->CastChecked<FRemoteControlOSCProtocolEntity>();

	TArray<FRemoteControlProtocolEntityWeakPtr>& BindingsEntitiesPtr = Bindings.FindOrAdd(OSCProtocolEntity->PathName);
	BindingsEntitiesPtr.Add(MoveTemp(InRemoteControlProtocolEntityPtr));
}

void FRemoteControlProtocolOSC::Unbind(FRemoteControlProtocolEntityPtr InRemoteControlProtocolEntityPtr)
{
	if (!ensure(InRemoteControlProtocolEntityPtr.IsValid()))
	{
		return;
	}

	FRemoteControlOSCProtocolEntity* OSCProtocolEntity = InRemoteControlProtocolEntityPtr->CastChecked<FRemoteControlOSCProtocolEntity>();	
	for (TPair<FName, TArray<FRemoteControlProtocolEntityWeakPtr>>& BindingsPair : Bindings)
	{
		BindingsPair.Value.RemoveAllSwap(CreateProtocolComparator(OSCProtocolEntity->GetPropertyId()));
	}
}

void FRemoteControlProtocolOSC::OSCReceivedMessageEvent(const FOSCMessage& Message, const FString& IPAddress, uint16 Port)
{
	const TSharedPtr<IOSCPacket>& Packet = Message.GetPacket();
	const FOSCAddress& Address = Message.GetAddress();

	const TArray<FRemoteControlProtocolEntityWeakPtr>* ProtocolEntityArrayPtr = Bindings.Find(*Address.GetFullPath());
	if (ProtocolEntityArrayPtr == nullptr)
	{
		return;
	}

	TArray<float> FloatValues;
	UOSCManager::GetAllFloats(Message, FloatValues);
	for (const float FloatValue : FloatValues)
	{
		for (const FRemoteControlProtocolEntityWeakPtr& BindingPtr : *ProtocolEntityArrayPtr)
		{
			if (const TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>> ProtocolEntityPtr = BindingPtr.Pin())
			{
				(*ProtocolEntityPtr)->ApplyProtocolValueToProperty(FloatValue);
			}
		}
	}
}

void FRemoteControlProtocolOSC::UnbindAll()
{
	Bindings.Empty();
}
