// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NetProfilerProvider.h"
#include "AnalysisServicePrivate.h"
#include "Common/StringStore.h"

namespace Trace
{

const FName FNetProfilerProvider::ProviderName("NetProfilerProvider");

FNetProfilerProvider::FNetProfilerProvider(IAnalysisSession& InSession)
	: Session(InSession)
	, Connections(InSession.GetLinearAllocator(), 4096)
	, ConnectionChangeCount(0u)
{
}

FNetProfilerProvider::~FNetProfilerProvider()
{
}

uint32 FNetProfilerProvider::AddNetProfilerName(TCHAR* Name)
{
	Session.WriteAccessCheck();
	
	FNetProfilerName& NewName = Names.AddDefaulted_GetRef();
	NewName.NameIndex = Names.Num() - 1;
	NewName.Name = Session.StoreString(Name);

	return NewName.NameIndex;
}

const FNetProfilerName* FNetProfilerProvider::GetNetProfilerName(uint32 NameIndex) const
{
	return NameIndex < (uint32)Names.Num() ? &Names[NameIndex] : nullptr;
}

Trace::FNetProfilerGameInstanceInternal& FNetProfilerProvider::CreateGameInstance()
{
	Session.WriteAccessCheck();

	FNetProfilerGameInstanceInternal& GameInstance = GameInstances.AddDefaulted_GetRef();
	GameInstance.Instance.GameInstanceIndex = GameInstances.Num() - 1;

	GameInstance.Objects = (TPagedArray<FNetProfilerObjectInstance>*)Session.GetLinearAllocator().Allocate(sizeof(TPagedArray<FNetProfilerObjectInstance>));
	new (GameInstance.Objects) TPagedArray<FNetProfilerObjectInstance>(Session.GetLinearAllocator(), 4096);
	GameInstance.ObjectsChangeCount = 0u;

	return GameInstance;
}

Trace::FNetProfilerGameInstanceInternal* FNetProfilerProvider::EditGameInstance(uint32 GameInstanceIndex)
{
	Session.WriteAccessCheck();

	if (ensure(GameInstanceIndex < (uint32)GameInstances.Num()))
	{
		return &GameInstances[GameInstanceIndex];
	}
	else
	{
		return nullptr;
	}
}

Trace::FNetProfilerConnectionInternal& FNetProfilerProvider::CreateConnection(uint32 GameInstanceIndex)
{
	Session.WriteAccessCheck();

	FNetProfilerGameInstanceInternal* GameInstance = EditGameInstance(GameInstanceIndex);
	check(GameInstance);

	// Create new connection
	uint32 ConnectionIndex = Connections.Num();
	FNetProfilerConnectionInternal& Connection = Connections.PushBack();

	Connection.Connection.ConnectionIndex = ConnectionIndex;
	Connection.Connection.GameIntanceIndex = GameInstanceIndex;
	Connection.Connection.bHasIncomingData = false;
	Connection.Connection.bHasOutgoingData = false;

	GameInstance->Connections.Push(ConnectionIndex);

	// Allocate storage for packets and events
	Connection.Data[ENetProfilerConnectionMode::Outgoing] = (FNetProfilerConnectionData*)Session.GetLinearAllocator().Allocate(sizeof(FNetProfilerConnectionData));
	new (Connection.Data[ENetProfilerConnectionMode::Outgoing]) FNetProfilerConnectionData(Session.GetLinearAllocator());

	Connection.Data[ENetProfilerConnectionMode::Incoming] = (FNetProfilerConnectionData*)Session.GetLinearAllocator().Allocate(sizeof(FNetProfilerConnectionData));
	new (Connection.Data[ENetProfilerConnectionMode::Incoming]) FNetProfilerConnectionData(Session.GetLinearAllocator());

	++ConnectionChangeCount;

	return Connection;
}

Trace::FNetProfilerObjectInstance& FNetProfilerProvider::CreateObject(uint32 GameInstanceIndex)
{
	Session.WriteAccessCheck();

	FNetProfilerGameInstanceInternal* GameInstance = EditGameInstance(GameInstanceIndex);
	check(GameInstance);

	Trace::FNetProfilerObjectInstance& Object = GameInstance->Objects->PushBack();
	Object.ObjectIndex = GameInstance->Objects->Num() - 1;
	++GameInstance->ObjectsChangeCount;

	return Object;
}

Trace::FNetProfilerObjectInstance* FNetProfilerProvider::EditObject(uint32 GameInstanceIndex, uint32 ObjectIndex)
{
	Session.WriteAccessCheck();

	FNetProfilerGameInstanceInternal* GameInstance = EditGameInstance(GameInstanceIndex);
	check(GameInstance);
	
	if (ensure(ObjectIndex < (uint32)GameInstance->Objects->Num()))
	{
		++GameInstance->ObjectsChangeCount;
		return &(*GameInstance->Objects)[ObjectIndex];
	}
	else
	{
		return nullptr;
	}
}

Trace::FNetProfilerConnectionInternal* FNetProfilerProvider::EditConnection(uint32 ConnectionIndex)
{
	Session.WriteAccessCheck();
	
	if (ensure(ConnectionIndex < (uint32)Connections.Num()))
	{
		++ConnectionChangeCount;
		return &Connections[ConnectionIndex];
	}
	else
	{
		return nullptr;
	}
}

void FNetProfilerProvider::EditPacketDeliveryStatus(uint32 ConnectionIndex, ENetProfilerConnectionMode Mode, uint32 SequenceNumber, ENetProfilerDeliveryStatus DeliveryStatus)
{
	check(ConnectionIndex < (uint32)Connections.Num());

	FNetProfilerConnectionInternal& Connection = Connections[ConnectionIndex];
	Trace::FNetProfilerConnectionData& Data = *Connections[ConnectionIndex].Data[Mode];

	// try to locate packet
	uint32 PacketCount = Data.Packets.Num();
	for (uint32 It=0; It < PacketCount; ++It)
	{
		const uint32 PacketIndex = PacketCount - It - 1u;
		if (Data.Packets[PacketIndex].SequenceNumber == SequenceNumber)
		{
			Data.Packets[PacketIndex].DeliveryStatus = DeliveryStatus;
			++Data.PacketChangeCount;
			return;
		}
	}
}

Trace::FNetProfilerConnectionData& FNetProfilerProvider::EditConnectionData(uint32 ConnectionIndex, ENetProfilerConnectionMode Mode)
{
	check(ConnectionIndex < (uint32)Connections.Num());

	FNetProfilerConnectionInternal& Connection = Connections[ConnectionIndex];

	if (Mode == ENetProfilerConnectionMode::Incoming && !Connection.Connection.bHasIncomingData)
	{
		Connection.Connection.bHasIncomingData = true;
		++ConnectionChangeCount;
	}
	if (Mode == ENetProfilerConnectionMode::Outgoing && !Connection.Connection.bHasOutgoingData)
	{
		Connection.Connection.bHasOutgoingData = true;
		++ConnectionChangeCount;
	}

	return *Connections[ConnectionIndex].Data[Mode];
}

void FNetProfilerProvider::ReadNames(TFunctionRef<void(const FNetProfilerName*, uint64)> Callback) const
{
	Session.ReadAccessCheck();

	Callback(Names.GetData(), Names.Num());
}

void FNetProfilerProvider::ReadName(uint32 NameIndex, TFunctionRef<void(const FNetProfilerName&)> Callback) const
{
	Session.ReadAccessCheck();
	check(NameIndex < (uint32)Names.Num());

	Callback(*GetNetProfilerName(NameIndex));
}

void FNetProfilerProvider::ReadGameInstances(TFunctionRef<void(const FNetProfilerGameInstance&)> Callback) const
{
	Session.ReadAccessCheck();

	for (const FNetProfilerGameInstanceInternal& Instance : GameInstances)
	{
		Callback(Instance.Instance);
	}
}

uint32 FNetProfilerProvider::GetConnectionCount(uint32 GameInstanceIndex) const
{
	Session.ReadAccessCheck();

	check(GameInstanceIndex < (uint32)GameInstances.Num());

	const FNetProfilerGameInstanceInternal& GameInstance = GameInstances[GameInstanceIndex];

	return GameInstance.Connections.Num();
}

void FNetProfilerProvider::ReadConnections(uint32 GameInstanceIndex, TFunctionRef<void(const FNetProfilerConnection&)> Callback) const
{
	Session.ReadAccessCheck();

	check(GameInstanceIndex < (uint32)GameInstances.Num());

	const FNetProfilerGameInstanceInternal& GameInstance = GameInstances[GameInstanceIndex];

	for (uint32 ConnectionIndex : MakeArrayView(GameInstance.Connections.GetData(), GameInstance.Connections.Num()))
	{
		Callback(Connections[ConnectionIndex].Connection);
	}
}

void FNetProfilerProvider::ReadConnection(uint32 ConnectionIndex, TFunctionRef<void(const FNetProfilerConnection&)> Callback) const
{
	Session.ReadAccessCheck();

	check(ConnectionIndex < Connections.Num());

	Callback(Connections[ConnectionIndex].Connection);
}

uint32 FNetProfilerProvider::GetObjectCount(uint32 GameInstanceIndex) const
{
	Session.ReadAccessCheck();

	check(GameInstanceIndex < (uint32)GameInstances.Num());

	const FNetProfilerGameInstanceInternal& GameInstance = GameInstances[GameInstanceIndex];

	return GameInstance.Objects->Num();
}

void FNetProfilerProvider::ReadObjects(uint32 GameInstanceIndex, TFunctionRef<void(const FNetProfilerObjectInstance&)> Callback) const
{
	Session.ReadAccessCheck();

	check(GameInstanceIndex < (uint32)GameInstances.Num());

	const FNetProfilerGameInstanceInternal& GameInstance = GameInstances[GameInstanceIndex];
	const auto& Objects = *GameInstance.Objects;

	for (uint32 ObjectsIt = 0, ObjectsEndIt = GameInstance.Objects->Num(); ObjectsIt < ObjectsEndIt; ++ObjectsIt)
	{
		Callback(Objects[ObjectsIt]);
	}
}

void FNetProfilerProvider::ReadObject(uint32 GameInstanceIndex, uint32 ObjectIndex, TFunctionRef<void(const FNetProfilerObjectInstance&)> Callback) const
{
	Session.ReadAccessCheck();

	check(GameInstanceIndex < (uint32)GameInstances.Num());

	const FNetProfilerGameInstanceInternal& GameInstance = GameInstances[GameInstanceIndex];

	check(ObjectIndex < GameInstance.Objects->Num());

	Callback((*GameInstance.Objects)[ObjectIndex]);
}

uint32 FNetProfilerProvider::GeObjectsChangeCount(uint32 GameInstanceIndex) const
{
	Session.ReadAccessCheck();

	check(GameInstanceIndex < (uint32)GameInstances.Num());

	const FNetProfilerGameInstanceInternal& GameInstance = GameInstances[GameInstanceIndex];

	return GameInstance.ObjectsChangeCount;
}

const INetProfilerProvider& ReadNetProfilerProvider(const IAnalysisSession& Session)
{
	Session.ReadAccessCheck();

	return *Session.ReadProvider<INetProfilerProvider>(FNetProfilerProvider::ProviderName);
}

uint32 FNetProfilerProvider::GetPacketCount(uint32 ConnectionIndex, ENetProfilerConnectionMode Mode) const
{
	Session.ReadAccessCheck();

	check(ConnectionIndex < Connections.Num());

	const auto& Packets = Connections[ConnectionIndex].Data[Mode]->Packets;
	
	return Packets.Num();
}

void FNetProfilerProvider::EnumeratePackets(uint32 ConnectionIndex, ENetProfilerConnectionMode Mode, uint32 PacketIndexIntervalStart, uint32 PacketIndexIntervalEnd, TFunctionRef<void(const FNetProfilerPacket&)> Callback) const
{
	Session.ReadAccessCheck();

	check(ConnectionIndex < Connections.Num());

	const auto& Packets = Connections[ConnectionIndex].Data[Mode]->Packets;
	
	const uint32 PacketCount = Packets.Num();

	if (PacketCount == 0 || PacketIndexIntervalStart > PacketIndexIntervalEnd)
	{
		return;
	}

	for (uint32 PacketIt = PacketIndexIntervalStart, PacketEndIt = FMath::Min(PacketIndexIntervalEnd, PacketCount - 1u); PacketIt <= PacketEndIt; ++PacketIt)
	{
		Callback(Packets[PacketIt]);
	}
}

// Enumerate packet content events by range
void FNetProfilerProvider::EnumeratePacketContentEventsByIndex(uint32 ConnectionIndex, ENetProfilerConnectionMode Mode, uint32 StartEventIndex, uint32 EndEventIndex, TFunctionRef<void(const FNetProfilerContentEvent&)> Callback) const
{
	Session.ReadAccessCheck();

	check(ConnectionIndex < Connections.Num());

	const auto& ContentEvents = Connections[ConnectionIndex].Data[Mode]->ContentEvents;
	
	const uint32 EventCount = ContentEvents.Num();

	if (EventCount == 0 || StartEventIndex > EndEventIndex)
	{
		return;
	}

	for (uint32 EventIt = StartEventIndex, EventEndIt = FMath::Min(EndEventIndex, EventCount - 1u); EventIt <= EventEndIt; ++EventIt)
	{
		Callback(ContentEvents[EventIt]);
	}
}

void FNetProfilerProvider::EnumeratePacketContentEventsByPosition(uint32 ConnectionIndex, ENetProfilerConnectionMode Mode, uint32 PacketIndex, uint32 StartPos, uint32 EndPos, TFunctionRef<void(const FNetProfilerContentEvent&)> Callback) const
{
	Session.ReadAccessCheck();

	check(ConnectionIndex < Connections.Num());

	 const FNetProfilerConnectionData& ConnectionData = * Connections[ConnectionIndex].Data[Mode];

	const FNetProfilerPacket& Packet = ConnectionData.Packets[PacketIndex];
	if (Packet.EventCount == 0u)
	{
		return;
	}

	const uint32 StartEventIndex = Packet.StartEventIndex;
	const uint32 EndEventIndex = StartEventIndex + Packet.EventCount - 1u;

	const auto& ContentEvents = ConnectionData.ContentEvents;
	
	uint32 EventIt = StartEventIndex; 
	// Skip all Events outside of the scope
	while (EventIt <= EndEventIndex &&	ContentEvents[EventIt].EndPos < StartPos)
	{
		++EventIt;
	}

	// Execute callback for all found events
	while (EventIt <= EndEventIndex && ContentEvents[EventIt].StartPos < EndPos)
	{
		Callback(ContentEvents[EventIt]);
		++EventIt;
	}
}

uint32 FNetProfilerProvider::GetPacketChangeCount(uint32 ConnectionIndex, ENetProfilerConnectionMode Mode) const
{
	Session.ReadAccessCheck();
	check(ConnectionIndex < Connections.Num());

	return Connections[ConnectionIndex].Data[Mode]->PacketChangeCount;
}

uint32 FNetProfilerProvider::GetPacketContentEventChangeCount(uint32 ConnectionIndex, ENetProfilerConnectionMode Mode) const
{
	Session.ReadAccessCheck();
	check(ConnectionIndex < Connections.Num());

	return Connections[ConnectionIndex].Data[Mode]->ContentEventChangeCount;
}

}
