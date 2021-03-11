// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlProtocolModule.h"
#include "RemoteControlProtocol.h"

#define LOCTEXT_NAMESPACE "RemoteControlProtocol"

void FRemoteControlProtocolModule::ShutdownModule()
{
	EmptyProtocols();
}

TArray<FName> FRemoteControlProtocolModule::GetProtocolNames() const
{
	TArray<FName> ProtocolNames;
	Protocols.GenerateKeyArray(ProtocolNames);
	return ProtocolNames;
}

TSharedPtr<IRemoteControlProtocol> FRemoteControlProtocolModule::GetProtocolByName(FName InProtocolName) const
{
	if (const TSharedRef<IRemoteControlProtocol>* ProtocolPtr = Protocols.Find(InProtocolName))
	{
		return *ProtocolPtr;
	}

	return nullptr;
}

void FRemoteControlProtocolModule::AddProtocol(FName InProtocolName, TSharedRef<IRemoteControlProtocol> InProtocol)
{
	InProtocol->Init();
	Protocols.Add(InProtocolName, MoveTemp(InProtocol));
}

void FRemoteControlProtocolModule::RemoveProtocol(FName InProtocolName, TSharedRef<IRemoteControlProtocol> InProtocol)
{
	if (TSharedRef<IRemoteControlProtocol>* Protocol = Protocols.Find(InProtocolName))
	{
		(*Protocol)->UnbindAll();
	}

	Protocols.Remove(InProtocolName);
}

void FRemoteControlProtocolModule::EmptyProtocols()
{
	for (TPair<FName, TSharedRef<IRemoteControlProtocol>>& ProtocolPair : Protocols)
	{
		ProtocolPair.Value->UnbindAll();
	}

	Protocols.Empty();
}


IMPLEMENT_MODULE(FRemoteControlProtocolModule, RemoteControlProtocol);

#undef LOCTEXT_NAMESPACE /*RemoteControlProtocol*/
