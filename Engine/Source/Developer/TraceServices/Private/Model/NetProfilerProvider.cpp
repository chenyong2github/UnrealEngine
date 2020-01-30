// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetProfilerProvider.h"
#include "AnalysisServicePrivate.h"
#include "Containers/ArrayView.h"
#include "Common/StringStore.h"

namespace Trace
{

const FName FNetProfilerProvider::ProviderName("NetProfilerProvider");

FNetProfilerProvider::FNetProfilerProvider(IAnalysisSession& InSession)
	: Session(InSession)
	, NetTraceVersion(0U)
	, Connections(InSession.GetLinearAllocator(), 4096)
	, ConnectionChangeCount(0u)
{
	// Use name index 0 to indicate that we do not know the name
	AddNetProfilerName(TEXT("N/A"));

	AggregatedStatsTableLayout.
		AddColumn(&FNetProfilerAggregatedStats::EventTypeIndex, TEXT("EventTypeIndex")).
		AddColumn(&FNetProfilerAggregatedStats::InstanceCount, TEXT("Count")).
		AddColumn(&FNetProfilerAggregatedStats::TotalInclusive, TEXT("Incl")).
		AddColumn(&FNetProfilerAggregatedStats::MaxInclusive, TEXT("I.Max")).
		AddColumn(&FNetProfilerAggregatedStats::AverageInclusive, TEXT("I.Avg")).
		AddColumn(&FNetProfilerAggregatedStats::TotalExclusive, TEXT("Excl")).
		AddColumn(&FNetProfilerAggregatedStats::MaxExclusive, TEXT("E.Max"));
}

FNetProfilerProvider::~FNetProfilerProvider()
{
}

void FNetProfilerProvider::SetNetTraceVersion(uint32 Version)
{
	Session.WriteAccessCheck();

	NetTraceVersion = Version;
}

uint32 FNetProfilerProvider::GetNetTraceVersion() const
{
	return NetTraceVersion;
}

uint32 FNetProfilerProvider::AddNetProfilerName(const TCHAR* Name)
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

uint32 FNetProfilerProvider::AddNetProfilerEventType(uint32 NameIndex, uint32 Level)
{
	Session.WriteAccessCheck();

	FNetProfilerEventType& NewEventType = EventTypes.AddDefaulted_GetRef();
	NewEventType.EventTypeIndex = EventTypes.Num() - 1;
	NewEventType.NameIndex = NameIndex;
	NewEventType.Name = GetNetProfilerName(NameIndex)->Name;
	NewEventType.Level = Level;

	return NewEventType.EventTypeIndex;
}

const FNetProfilerEventType* FNetProfilerProvider::GetNetProfilerEventType(uint32 EventTypeIndex) const
{
	return EventTypeIndex < (uint32)EventTypes.Num() ? &EventTypes[EventTypeIndex] : nullptr;
}

Trace::FNetProfilerGameInstanceInternal& FNetProfilerProvider::CreateGameInstance()
{
	Session.WriteAccessCheck();

	FNetProfilerGameInstanceInternal& GameInstance = GameInstances.AddDefaulted_GetRef();
	GameInstance.Instance.GameInstanceIndex = GameInstances.Num() - 1;

	GameInstance.Objects = (TPagedArray<FNetProfilerObjectInstance>*)Session.GetLinearAllocator().Allocate(sizeof(TPagedArray<FNetProfilerObjectInstance>));
	new (GameInstance.Objects) TPagedArray<FNetProfilerObjectInstance>(Session.GetLinearAllocator(), 4096);
	GameInstance.ObjectsChangeCount = 0u;

	// We reserve object index 0 as an invalid object
	CreateObject(GameInstance.Instance.GameInstanceIndex);

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
	Connection.Connection.GameInstanceIndex = GameInstanceIndex;
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

void FNetProfilerProvider::ReadEventTypes(TFunctionRef<void(const FNetProfilerEventType*, uint64)> Callback) const
{
	Session.ReadAccessCheck();

	Callback(EventTypes.GetData(), EventTypes.Num());
}

void FNetProfilerProvider::ReadEventType(uint32 EventTypeIndex, TFunctionRef<void(const FNetProfilerEventType&)> Callback) const
{
	Session.ReadAccessCheck();
	check(EventTypeIndex < (uint32)EventTypes.Num());

	Callback(*GetNetProfilerEventType(EventTypeIndex));
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

uint32 FNetProfilerProvider::GetObjectsChangeCount(uint32 GameInstanceIndex) const
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

ITable<FNetProfilerAggregatedStats>* FNetProfilerProvider::CreateAggregation(uint32 ConnectionIndex, ENetProfilerConnectionMode Mode, uint32 PacketIndexIntervalStart, uint32 PacketIndexIntervalEnd, uint32 StartPosition, uint32 EndPosition) const
{
	Session.ReadAccessCheck();

	if (!ensure(ConnectionIndex < Connections.Num()))
	{
		return nullptr;
	}

	if (!ensure(PacketIndexIntervalStart <= PacketIndexIntervalEnd))
	{
		return nullptr;
	}

	const auto& Packets = Connections[ConnectionIndex].Data[Mode]->Packets;
	const uint32 PacketCount = Packets.Num();

	if (!ensure(PacketCount > 0))
	{
		return nullptr;
	}

	// Aggregate stats for events in selection
	TMap<uint32, FNetProfilerAggregatedStats> AggregatedStats;
	AggregatedStats.Reserve(EventTypes.Num());

	struct FStackEntry
	{
		uint32 EventTypeIndex = 0U;
		uint32 StartPos = 0U;
		uint32 EndPos = 0U;
		uint32 ExclusiveAccumulator = 0U;
	};

	TArray<FStackEntry> Stack;
	Stack.SetNum(256);

	auto GetStatsFunction = [&AggregatedStats, &Stack, this](const FNetProfilerContentEvent& ContentEvent)
	{
		FNetProfilerAggregatedStats* StatsEntry = AggregatedStats.Find(ContentEvent.EventTypeIndex);
		if (!StatsEntry)
		{
			StatsEntry = &AggregatedStats.Add(ContentEvent.EventTypeIndex);
			StatsEntry->EventTypeIndex = ContentEvent.EventTypeIndex;
		}

		// Fill in basics
		uint32 InclusiveSize = ContentEvent.EndPos - ContentEvent.StartPos;

		++StatsEntry->InstanceCount;
		StatsEntry->TotalInclusive += InclusiveSize;
		StatsEntry->MaxInclusive = FMath::Max(InclusiveSize, StatsEntry->MaxInclusive);

		// We track what we have visited to be able to update exclusive bits for our parent
		// We accumulate during the first pass and finalize the values during the second
		Stack[ContentEvent.Level].EventTypeIndex = ContentEvent.EventTypeIndex;
		Stack[ContentEvent.Level].StartPos = ContentEvent.StartPos;
		Stack[ContentEvent.Level].EndPos = ContentEvent.EndPos;
		Stack[ContentEvent.Level].ExclusiveAccumulator = 0U;

		if (ContentEvent.Level > 0U)
		{
			FStackEntry& ParentStackEntry = Stack[ContentEvent.Level - 1U];

			ParentStackEntry.ExclusiveAccumulator += InclusiveSize;

			if (ContentEvent.EndPos == ParentStackEntry.EndPos)
			{
				// Finalize exclusive
				FNetProfilerAggregatedStats& ParentStateEntry = AggregatedStats.FindChecked(ContentEvent.EventTypeIndex);
				uint32 ExclusiveTime = (ParentStackEntry.EndPos - ParentStackEntry.StartPos) - ParentStackEntry.ExclusiveAccumulator;
				ParentStateEntry.TotalExclusive += ExclusiveTime;
				ParentStateEntry.MaxExclusive = FMath::Max(ParentStateEntry.MaxExclusive, ExclusiveTime);
			}
		}
	};

	// Iterate over content events
	if (PacketIndexIntervalStart == PacketIndexIntervalEnd)
	{
		EnumeratePacketContentEventsByPosition(ConnectionIndex, Mode, PacketIndexIntervalStart, StartPosition, EndPosition, GetStatsFunction);
	}
	else
	{
		// Iterate over packets
		for (uint32 PacketIt = PacketIndexIntervalStart, PacketEndIt = FMath::Min(PacketIndexIntervalEnd, PacketCount - 1u); PacketIt <= PacketEndIt; ++PacketIt)
		{
			const auto& ContentEvents = Connections[ConnectionIndex].Data[Mode]->ContentEvents;
			const uint32 EventCount = ContentEvents.Num();

			const FNetProfilerPacket& Packet = Packets[PacketIt];
			if (Packet.EventCount > 0U)
			{
				const uint32 StartEventIndex = Packet.StartEventIndex;
				for (uint32 It = 0U; It < Packet.EventCount; ++It)
				{
					GetStatsFunction(ContentEvents[StartEventIndex + It]);
				}
			}
		}
	}

	// Calculate averages and populate table
	TTable<FNetProfilerAggregatedStats>* Table = new TTable<FNetProfilerAggregatedStats>(AggregatedStatsTableLayout);
	for (const auto& KV : AggregatedStats)
	{
		FNetProfilerAggregatedStats& Row = Table->AddRow();
		Row = KV.Value;

		// Finalize StatsEntry
		Row.AverageInclusive = (uint64)((double)KV.Value.TotalInclusive / KV.Value.InstanceCount);
	}
	return Table;
}

}
