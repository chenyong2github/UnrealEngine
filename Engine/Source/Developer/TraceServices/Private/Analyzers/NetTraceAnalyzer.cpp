// Copyright Epic Games, Inc. All Rights Reserved.
#include "NetTraceAnalyzer.h"
#include "AnalysisServicePrivate.h"
#include "Common/Utils.h"
#include "TraceServices/Model/Threads.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogNetTrace, Log, All);
DEFINE_LOG_CATEGORY(LogNetTrace);

enum ENetTraceAnalyzerVersion
{
	ENetTraceAnalyzerVersion_Initial = 1,
	ENetTraceAnalyzerVersion_BunchChannelIndex = 2,
	ENetTraceAnalyzerVersion_BunchChannelInfo = 3,
};


FNetTraceAnalyzer::FNetTraceAnalyzer(Trace::IAnalysisSession& InSession, Trace::FNetProfilerProvider& InNetProfilerProvider)
	: Session(InSession)
	, NetProfilerProvider(InNetProfilerProvider)
	, NetTraceVersion(0)
	, NetTraceReporterVersion(0)
{
}

void FNetTraceAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_InitEvent, "NetTrace", "InitEvent");
	Builder.RouteEvent(RouteId_InstanceDestroyedEvent, "NetTrace", "InstanceDestroyedEvent");
	Builder.RouteEvent(RouteId_NameEvent, "NetTrace", "NameEvent");
	Builder.RouteEvent(RouteId_PacketContentEvent, "NetTrace", "PacketContentEvent");
	Builder.RouteEvent(RouteId_PacketEvent, "NetTrace", "PacketEvent");
	Builder.RouteEvent(RouteId_PacketDroppedEvent, "NetTrace", "PacketDroppedEvent");
	Builder.RouteEvent(RouteId_ConnectionCreatedEvent, "NetTrace", "ConnectionCreatedEvent");
	// Add some default event types that we use for generic type events to make it easier to extend
	// ConnectionAdded/Removed connections state /name etc?
	Builder.RouteEvent(RouteId_ConnectionClosedEvent, "NetTrace", "ConnectionClosedEvent");
	Builder.RouteEvent(RouteId_ObjectCreatedEvent, "NetTrace", "ObjectCreatedEvent");
	Builder.RouteEvent(RouteId_ObjectDestroyedEvent, "NetTrace", "ObjectDestroyedEvent");

	// Default names
	{
		Trace::FAnalysisSessionEditScope _(Session);
		BunchHeaderNameIndex = NetProfilerProvider.AddNetProfilerName(TEXT("BunchHeader"));
	}
}

void FNetTraceAnalyzer::OnAnalysisEnd()
{
}

uint32 FNetTraceAnalyzer::GetTracedEventTypeIndex(uint16 NameIndex, uint8 Level)
{
	const uint32 TracedEventTypeKey = uint32(NameIndex << 8U | Level);

	if (const uint32* NetProfilerEventTypeIndex = TraceEventTypeToNetProfilerEventTypeIndexMap.Find(TracedEventTypeKey))
	{
		return *NetProfilerEventTypeIndex;
	}
	else
	{
		// Add new EventType
		uint32 NewEventTypeIndex = NetProfilerProvider.AddNetProfilerEventType(NameIndex, Level);
		TraceEventTypeToNetProfilerEventTypeIndexMap.Add(TracedEventTypeKey, NewEventTypeIndex);

		return NewEventTypeIndex;
	}
}

bool FNetTraceAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session);

	// check that we always get the InitEvent before processing any other events
	if (!ensure(RouteId == RouteId_InitEvent || NetTraceVersion > 0))
	{
		return false;
	}

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
		case RouteId_InitEvent:
		{
			const uint64 TimestampCycles = EventData.GetValue<uint64>("Timestamp");
			LastTimeStamp = Context.EventTime.AsSeconds(TimestampCycles);

			// we always trace the version so that we make sure that we are backwards compatible with older trace stream
			NetTraceVersion = EventData.GetValue<uint32>("NetTraceVersion");
			NetTraceReporterVersion = EventData.GetValue<uint32>("NetTraceReporterVersion");
			
			NetProfilerProvider.SetNetTraceVersion(NetTraceVersion);
		}
		break;

		case RouteId_InstanceDestroyedEvent:
		{
			const uint8 GameInstanceId = EventData.GetValue<uint8>("GameInstanceId");
			DestroyActiveGameInstanceState(GameInstanceId);
		}
		break;

		case RouteId_NameEvent:
		{
			uint16 TraceNameId = EventData.GetValue<uint16>("NameId");
			if (TracedNameIdToNetProfilerNameIdMap.Contains(TraceNameId))
			{
				// need to update the name
				check(false);
			}
			else
			{
				TracedNameIdToNetProfilerNameIdMap.Add(TraceNameId, NetProfilerProvider.AddNetProfilerName(UTF8_TO_TCHAR(EventData.GetAttachment())));
			}
		}
		break;

		case RouteId_PacketContentEvent:
		{
			HandlePacketContentEvent(Context, EventData);
		}
		break;

		case RouteId_PacketEvent:
		{
			HandlePacketEvent(Context, EventData);
		}
		break;

		case RouteId_PacketDroppedEvent:
		{
			HandlePacketDroppedEvent(Context, EventData);
		}
		break;

		case RouteId_ConnectionCreatedEvent:
		{
			HandleConnectionCretedEvent(Context, EventData);
		}
		break;

		case RouteId_ConnectionClosedEvent:
		{
			HandleConnectionClosedEvent(Context, EventData);
		}
		break;

		case RouteId_ObjectCreatedEvent:
		{
			HandleObjectCreatedEvent(Context, EventData);
		}
		break;

		case RouteId_ObjectDestroyedEvent:
		{
			HandleObjectDestroyedEvent(Context, EventData);
		}
		break;
	}

	return true;
}

void FNetTraceAnalyzer::HandlePacketContentEvent(const FOnEventContext& Context, const FEventData& EventData)
{
	const uint16 ConnectionId = EventData.GetValue<uint16>("ConnectionId");
	const uint8 GameInstanceId = EventData.GetValue<uint8>("GameInstanceId");
	const uint8 PacketType =  EventData.GetValue<uint8>("PacketType");

	//UE_LOG(LogNetTrace, Display, TEXT("FNetTraceAnalyzer::HandlePacketContentEvent: GameInstanceId: %u, ConnectionId: %u, %s"), GameInstanceId, ConnectionId, PacketType ? TEXT("Incoming") : TEXT("Outgoing"));

	TSharedRef<FNetTraceGameInstanceState> GameInstanceState = GetOrCreateActiveGameInstanceState(GameInstanceId);
	FNetTraceConnectionState* ConnectionState = GetActiveConnectionState(GameInstanceId, ConnectionId);
	if (!ConnectionState)
	{
		return;
	}

	const Trace::ENetProfilerConnectionMode ConnectionMode = Trace::ENetProfilerConnectionMode(PacketType);
	Trace::FNetProfilerConnectionData& ConnectionData = NetProfilerProvider.EditConnectionData(ConnectionState->ConnectionIndex, ConnectionMode);
	++ConnectionData.ContentEventChangeCount;

	TArray<Trace::FNetProfilerContentEvent>& Events = (ConnectionState->BunchEvents)[ConnectionMode];
	TArray<FBunchInfo>& BunchInfos = (ConnectionState->BunchInfos)[ConnectionMode];

	// Decode batched events
	uint64 BufferSize = EventData.GetAttachmentSize();
	const uint8* BufferPtr = EventData.GetAttachment();
	const uint8* BufferEnd = BufferPtr + BufferSize;
	uint64 LastOffset = 0;

	uint64 CurrentBunchOffset = 0U;

	while (BufferPtr < BufferEnd)
	{
		// Decode data
		const EContentEventType DecodedEventType = EContentEventType(*BufferPtr++);
		switch (DecodedEventType)
		{
			case EContentEventType::Object:
			case EContentEventType::NameId:
			{
				Trace::FNetProfilerContentEvent& Event = Events.Emplace_GetRef();

				const uint8 DecodedNestingLevel = *BufferPtr++;

				const uint64 DecodedNameOrObjectId = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
				const uint64 DecodedEventStartPos = FTraceAnalyzerUtils::Decode7bit(BufferPtr) + LastOffset;
				LastOffset = DecodedEventStartPos;
				const uint64 DecodedEventEndPos = FTraceAnalyzerUtils::Decode7bit(BufferPtr) + DecodedEventStartPos;

				// Fill in event data
				Event.StartPos = DecodedEventStartPos + CurrentBunchOffset;
				Event.EndPos = DecodedEventEndPos + CurrentBunchOffset;
				Event.Level = DecodedNestingLevel;

				Event.ObjectInstanceIndex = 0;
				Event.NameIndex = 0;
				Event.BunchInfo.Value = 0;

				checkSlow(Event.EndPos > Event.StartPos);

				if (DecodedEventType == EContentEventType::Object)
				{
					// Object index, need to lookup name indirectly
					if (const FNetTraceActiveObjectState* ActiveObjectState = GameInstanceState->ActiveObjects.Find(DecodedNameOrObjectId))
					{
						Event.NameIndex = ActiveObjectState->NameIndex;
						Event.ObjectInstanceIndex = ActiveObjectState->ObjectIndex;
					}
				}
				else if (DecodedEventType == EContentEventType::NameId)
				{
					if (const uint32* NetProfilerNameIndex = TracedNameIdToNetProfilerNameIdMap.Find(DecodedNameOrObjectId))
					{
						Event.NameIndex = *NetProfilerNameIndex;
					}
					else
					{
						UE_LOG(LogNetTrace, Warning, TEXT("PacketContentEvent GameInstanceId: %u, ConnectionId: %u %s, Missing NameIndex: %u"), GameInstanceId, ConnectionId, ConnectionMode ? TEXT("Incoming") : TEXT("Outgoing"), DecodedNameOrObjectId);	
					}
				}

				// EventTypeIndex does not match NameIndex as we might see the same name on different levels
				Event.EventTypeIndex = GetTracedEventTypeIndex(Event.NameIndex, Event.Level);
			}
			break;

			case EContentEventType::BunchEvent:
			{
				const uint64 DecodedNameId = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
				const uint64 DecodedEventStartPos = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
				const uint64 DecodedEventEndPos = FTraceAnalyzerUtils::Decode7bit(BufferPtr) + DecodedEventStartPos;
				
				const uint32* NetProfilerNameIndex = DecodedNameId ? TracedNameIdToNetProfilerNameIdMap.Find(DecodedNameId) : nullptr;

				FBunchInfo BunchInfo;

				BunchInfo.BunchInfo.Value = 0;
				BunchInfo.HeaderBits = 0U;
				BunchInfo.BunchBits = DecodedEventEndPos;
				BunchInfo.FirstBunchEventIndex = Events.Num();
				BunchInfo.NameIndex = NetProfilerNameIndex ? *NetProfilerNameIndex : 0U;

				BunchInfos.Add(BunchInfo);

				// Must reset LastOffset after reading bunch data
				LastOffset = 0U;
			}
			break;

			case EContentEventType::BunchHeaderEvent:
			{
				const uint64 DecodedEventCount = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
				const uint64 DecodedHeaderBits = FTraceAnalyzerUtils::Decode7bit(BufferPtr);

				FBunchInfo& BunchInfo = BunchInfos.Last();

				BunchInfo.EventCount = DecodedEventCount;
				BunchInfo.FirstBunchEventIndex = Events.Num() - DecodedEventCount;

				// A bunch with header bits set is an actual bunch
				if (DecodedHeaderBits)
				{
					if (NetTraceVersion >= ENetTraceAnalyzerVersion_BunchChannelIndex)
					{
						const uint64 DecodedBunchInfo = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
						if (NetTraceVersion >= ENetTraceAnalyzerVersion_BunchChannelInfo)
						{
							BunchInfo.BunchInfo.Value = DecodedBunchInfo;
						}
						else
						{
							BunchInfo.BunchInfo.Value = uint64(0);
							BunchInfo.BunchInfo.ChannelIndex = DecodedBunchInfo;
						}

						if (BunchInfo.NameIndex)
						{
							GameInstanceState->ChannelNames.FindOrAdd(BunchInfo.BunchInfo.ChannelIndex) = BunchInfo.NameIndex;
						}
						else
						{
							const uint32* ExistingChannelNameIndex = GameInstanceState->ChannelNames.Find(BunchInfo.BunchInfo.ChannelIndex);						
							BunchInfo.NameIndex = ExistingChannelNameIndex ? *ExistingChannelNameIndex : 0U;
						}

						BunchInfo.BunchInfo.bIsValid = 1U;
					}

					BunchInfo.HeaderBits = DecodedHeaderBits;
					CurrentBunchOffset = 0U;
				}
				else
				{
					// Merged bunch, set offset for events
					CurrentBunchOffset = BunchInfo.BunchBits;
				}
			}
			break;
		};

	}
	check(BufferPtr == BufferEnd);
}

void FNetTraceAnalyzer::AddEvent(TPagedArray<Trace::FNetProfilerContentEvent>& Events, const Trace::FNetProfilerContentEvent& InEvent, uint32 Offset, uint32 LevelOffset)
{
	Trace::FNetProfilerContentEvent& Event = Events.PushBack();
	
	Event.EventTypeIndex = GetTracedEventTypeIndex(InEvent.NameIndex, InEvent.Level + LevelOffset);
	Event.NameIndex =  InEvent.NameIndex;
	Event.ObjectInstanceIndex = InEvent.ObjectInstanceIndex;
	Event.StartPos = InEvent.StartPos + Offset;
	Event.EndPos = InEvent.EndPos + Offset;
	Event.Level = InEvent.Level + LevelOffset;	
	Event.BunchInfo = InEvent.BunchInfo;
}

void FNetTraceAnalyzer::AddEvent(TPagedArray<Trace::FNetProfilerContentEvent>& Events, uint32 StartPos, uint32 EndPos, uint32 Level, uint32 NameIndex, Trace::FNetProfilerBunchInfo BunchInfo)
{
	Trace::FNetProfilerContentEvent& Event = Events.PushBack();

	Event.EventTypeIndex = GetTracedEventTypeIndex(NameIndex, Level);
	Event.NameIndex = NameIndex; 
	Event.ObjectInstanceIndex = 0;
	Event.StartPos = StartPos;
	Event.EndPos = EndPos;			
	Event.Level = Level;
	Event.BunchInfo = BunchInfo;
}

void FNetTraceAnalyzer::FlushPacketEvents(FNetTraceConnectionState& ConnectionState, Trace::FNetProfilerConnectionData& ConnectionData, const Trace::ENetProfilerConnectionMode ConnectionMode)
{
	TPagedArray<Trace::FNetProfilerContentEvent>& Events = ConnectionData.ContentEvents;

	TArray<Trace::FNetProfilerContentEvent>& BunchEvents = ConnectionState.BunchEvents[ConnectionMode];
	const int32 NumPacketEvents = BunchEvents.Num();

	int32 CurrentBunchEventIndex = 0;

	// Track bunch offsets
	uint32 NextBunchOffset = 0U;
	uint32 NextEventOffset = 0U;

	int32 NonBunchEventCount = ConnectionState.BunchInfos[ConnectionMode].Num() ? ConnectionState.BunchInfos[ConnectionMode][0].FirstBunchEventIndex : BunchEvents.Num();

	// Inject any events reported before the first bunch
	while (CurrentBunchEventIndex < NonBunchEventCount)
	{
		const Trace::FNetProfilerContentEvent& BunchEvent = BunchEvents[CurrentBunchEventIndex];

		AddEvent(Events, BunchEvent, 0U, 0U);
	
		NextBunchOffset = FMath::Max<uint64>(BunchEvent.EndPos, NextBunchOffset);
		++CurrentBunchEventIndex;
	}

	uint32 EventsToAdd = 0U;
	for (const FBunchInfo& Bunch : ConnectionState.BunchInfos[ConnectionMode])
	{
		uint32 BunchOffset = NextBunchOffset + Bunch.HeaderBits;
		EventsToAdd += Bunch.EventCount;

		// Report events for committed bunches
		if (Bunch.HeaderBits)
		{
			// Bunch event
			AddEvent(Events, NextBunchOffset, NextBunchOffset + Bunch.HeaderBits + Bunch.BunchBits, 0, Bunch.NameIndex, Bunch.BunchInfo);

			// Bunch header event
			AddEvent(Events, NextBunchOffset, NextBunchOffset + Bunch.HeaderBits, 1, BunchHeaderNameIndex, Trace::FNetProfilerBunchInfo::MakeBunchInfo(0));
	
			// Add events belonging to bunch, including the ones from merged bunches
			for (uint32 EventIt = 0; EventIt < EventsToAdd; ++EventIt)
			{
				const Trace::FNetProfilerContentEvent& BunchEvent = BunchEvents[CurrentBunchEventIndex];

				AddEvent(Events, BunchEvent, BunchOffset, 1U);
				++CurrentBunchEventIndex;
			}

			// Accumulate offset
			NextBunchOffset += Bunch.BunchBits + Bunch.HeaderBits;
			NextEventOffset = NextBunchOffset;

			// Reset event count
			EventsToAdd = 0U;
		}
		
		++ConnectionData.ContentEventChangeCount;
	}

	ConnectionState.BunchEvents[ConnectionMode].Reset();
	ConnectionState.BunchInfos[ConnectionMode].Reset();
}

void FNetTraceAnalyzer::HandlePacketEvent(const FOnEventContext& Context, const FEventData& EventData)
{
	const uint64 TimestampCycles = EventData.GetValue<uint64>("Timestamp");
	const uint32 PacketBits = EventData.GetValue<uint32>("PacketBits");
	const uint32 SequenceNumber = EventData.GetValue<uint32>("SequenceNumber");
	const uint8 GameInstanceId = EventData.GetValue<uint8>("GameInstanceId");
	const uint16 ConnectionId = EventData.GetValue<uint16>("ConnectionId");
	const uint8 PacketType = EventData.GetValue<uint8>("PacketType");

	const Trace::ENetProfilerConnectionMode ConnectionMode = Trace::ENetProfilerConnectionMode(PacketType);

	// Update LastTimestamp, later on we will be able to get timestamps piggybacked from other analyzers
	LastTimeStamp = Context.EventTime.AsSeconds(TimestampCycles);

	FNetTraceConnectionState* ConnectionState = GetActiveConnectionState(GameInstanceId, ConnectionId);
	if (!ConnectionState)
	{
		return;
	}

	// Add the packet
	Trace::FNetProfilerConnectionData& ConnectionData = NetProfilerProvider.EditConnectionData(ConnectionState->ConnectionIndex, ConnectionMode);
	Trace::FNetProfilerPacket& Packet = ConnectionData.Packets.PushBack();
	++ConnectionData.PacketChangeCount;

	// Flush packet events
	FlushPacketEvents(*ConnectionState, ConnectionData, ConnectionMode);

	// Fill in packet data a packet must have at least 1 event?
	Packet.StartEventIndex = ConnectionState->CurrentPacketStartIndex[ConnectionMode];
	Packet.EventCount = ConnectionData.ContentEvents.Num() - Packet.StartEventIndex;
	Packet.TimeStamp = GetLastTimestamp();
	Packet.SequenceNumber = SequenceNumber;
	Packet.DeliveryStatus = Trace::ENetProfilerDeliveryStatus::Unknown;

	Packet.ContentSizeInBits = PacketBits;
	Packet.TotalPacketSizeInBytes = (Packet.ContentSizeInBits + 7u) >> 3u;
	Packet.DeliveryStatus = Trace::ENetProfilerDeliveryStatus::Delivered;

	// Mark the beginning of a new packet
	ConnectionState->CurrentPacketStartIndex[ConnectionMode] = ConnectionData.ContentEvents.Num();
	ConnectionState->CurrentPacketBitOffset[ConnectionMode] = 0U;

	//UE_LOG(LogNetTrace, Log, TEXT("PacketEvent GameInstanceId: %u, ConnectionId: %u, %s, Seq: %u PacketBits: %u"), GameInstanceId, ConnectionId, ConnectionMode ? TEXT("Incoming") : TEXT("Outgoing"), SequenceNumber, Packet.ContentSizeInBits);
}

void FNetTraceAnalyzer::HandlePacketDroppedEvent(const FOnEventContext& Context, const FEventData& EventData)
{
	const uint64 TimestampCycles = EventData.GetValue<uint64>("Timestamp");
	const uint32 SequenceNumber = EventData.GetValue<uint32>("SequenceNumber");
	const uint8 GameInstanceId = EventData.GetValue<uint8>("GameInstanceId");
	const uint16 ConnectionId = EventData.GetValue<uint16>("ConnectionId");
	const uint8 PacketType = EventData.GetValue<uint8>("PacketType");

	// Update LastTimestamp, later on we will be able to get timestamps piggybacked from other analyzers
	LastTimeStamp = Context.EventTime.AsSeconds(TimestampCycles);

	FNetTraceConnectionState* ConnectionState = GetActiveConnectionState(GameInstanceId, ConnectionId);
	if (!ConnectionState)
	{
		return;
	}

	Trace::FNetProfilerConnectionData& ConnectionData = NetProfilerProvider.EditConnectionData(ConnectionState->ConnectionIndex, Trace::ENetProfilerConnectionMode(PacketType));

	// Update packet delivery status
	NetProfilerProvider.EditPacketDeliveryStatus(ConnectionState->ConnectionIndex, Trace::ENetProfilerConnectionMode(PacketType), SequenceNumber, Trace::ENetProfilerDeliveryStatus::Dropped);
}

void FNetTraceAnalyzer::HandleConnectionCretedEvent(const FOnEventContext& Context, const FEventData& EventData)
{
	const uint8 GameInstanceId = EventData.GetValue<uint8>("GameInstanceId");
	const uint16 ConnectionId = EventData.GetValue<uint16>("ConnectionId");

	TSharedRef<FNetTraceGameInstanceState> GameInstanceState = GetOrCreateActiveGameInstanceState(GameInstanceId);
	check(!GameInstanceState->ActiveConnections.Contains(ConnectionId));

	// Add to both active connections and to persistent connections
 	Trace::FNetProfilerConnectionInternal& Connection = NetProfilerProvider.CreateConnection(GameInstanceState->GameInstanceIndex);
	TSharedRef<FNetTraceConnectionState> ConnectionState = MakeShared<FNetTraceConnectionState>();
	GameInstanceState->ActiveConnections.Add(ConnectionId, ConnectionState);

	// Fill in Connection data
	Connection.Connection.ConnectionId = ConnectionId;
	Connection.Connection.LifeTime.Begin =  GetLastTimestamp();
	ConnectionState->ConnectionIndex = Connection.Connection.ConnectionIndex;
	ConnectionState->CurrentPacketStartIndex[Trace::ENetProfilerConnectionMode::Outgoing] = 0U;
	ConnectionState->CurrentPacketStartIndex[Trace::ENetProfilerConnectionMode::Incoming] = 0U;

	ConnectionState->CurrentPacketBitOffset[Trace::ENetProfilerConnectionMode::Outgoing] = 0U;
	ConnectionState->CurrentPacketBitOffset[Trace::ENetProfilerConnectionMode::Incoming] = 0U;
}

void FNetTraceAnalyzer::HandleConnectionClosedEvent(const FOnEventContext& Context, const FEventData& EventData)
{
	const uint8 GameInstanceId = EventData.GetValue<uint8>("GameInstanceId");
	const uint16 ConnectionId = EventData.GetValue<uint16>("ConnectionId");

	TSharedRef<FNetTraceGameInstanceState> GameInstanceState = GetOrCreateActiveGameInstanceState(GameInstanceId);

	if (TSharedRef<FNetTraceConnectionState>* ConnectionState = GameInstanceState->ActiveConnections.Find(ConnectionId))
	{
		if (Trace::FNetProfilerConnectionInternal* Connection = NetProfilerProvider.EditConnection((*ConnectionState)->ConnectionIndex))
		{
			// Update connection state
			Connection->Connection.LifeTime.End =  GetLastTimestamp();
		}
		GameInstanceState->ActiveConnections.Remove(ConnectionId);
	}
	else
	{
		// Incomplete trace?  Ignore?
		check(false);
	}
}

void FNetTraceAnalyzer::HandleObjectCreatedEvent(const FOnEventContext& Context, const FEventData& EventData)
{
	const uint64 TypeId = EventData.GetValue<uint64>("TypeId");
	const uint32 ObjectId = EventData.GetValue<uint32>("ObjectId");
	const uint32 OwnerId = EventData.GetValue<uint32>("OwnerId");
	const uint16 NameId = EventData.GetValue<uint16>("NameId");
	const uint8 GameInstanceId = EventData.GetValue<uint8>("GameInstanceId");

	TSharedRef<FNetTraceGameInstanceState> GameInstanceState = GetOrCreateActiveGameInstanceState(GameInstanceId);
	const uint32* NetProfilerNameIndex = TracedNameIdToNetProfilerNameIdMap.Find(NameId);
	const uint32 NameIndex = NetProfilerNameIndex ? *NetProfilerNameIndex : 0u;
	
	if (GameInstanceState->ActiveObjects.Contains(ObjectId))
	{
		if (Trace::FNetProfilerObjectInstance* ExistingInstance = NetProfilerProvider.EditObject(GameInstanceState->GameInstanceIndex, GameInstanceState->ActiveObjects[ObjectId].ObjectIndex))
		{
			if (ExistingInstance->NameIndex == NameIndex)
			{
				// Update existing object instance
				ExistingInstance->LifeTime.Begin = GetLastTimestamp();
				ExistingInstance->NetId = ObjectId;
				ExistingInstance->TypeId = TypeId;

				return;
			}

			// End instance and remove it from ActiveObjects
			ExistingInstance->LifeTime.End = GetLastTimestamp();
			GameInstanceState->ActiveObjects.Remove(ObjectId);
		}
	}

	// Add persistent object representation
	Trace::FNetProfilerObjectInstance& ObjectInstance = NetProfilerProvider.CreateObject(GameInstanceState->GameInstanceIndex);

	// Fill in object data
	ObjectInstance.LifeTime.Begin =  GetLastTimestamp();
	ObjectInstance.NameIndex = NameIndex;
	ObjectInstance.NetId = ObjectId;
	ObjectInstance.TypeId = TypeId;

	// Add to active objects
	GameInstanceState->ActiveObjects.Add(ObjectId, { ObjectInstance.ObjectIndex, ObjectInstance.NameIndex });
}

void FNetTraceAnalyzer::HandleObjectDestroyedEvent(const FOnEventContext& Context, const FEventData& EventData)
{
	// Remove from active instances and mark the end timestamp in the persistent instance list
	const uint8 GameInstanceId = EventData.GetValue<uint8>("GameInstanceId");
	const uint32 ObjectId = EventData.GetValue<uint32>("ObjectId");

	TSharedRef<FNetTraceGameInstanceState> GameInstanceState = GetOrCreateActiveGameInstanceState(GameInstanceId);

	FNetTraceActiveObjectState DestroyedObjectState;
	if (GameInstanceState->ActiveObjects.RemoveAndCopyValue(ObjectId, DestroyedObjectState))
	{
		if (Trace::FNetProfilerObjectInstance* ObjectInstance = NetProfilerProvider.EditObject(GameInstanceState->GameInstanceIndex, DestroyedObjectState.ObjectIndex))
		{
			// Update object data
			ObjectInstance->LifeTime.End = GetLastTimestamp();
		}
	}
}

TSharedRef<FNetTraceAnalyzer::FNetTraceGameInstanceState> FNetTraceAnalyzer::GetOrCreateActiveGameInstanceState(uint32 GameInstanceId)
{
	if (TSharedRef<FNetTraceAnalyzer::FNetTraceGameInstanceState>* FoundState = ActiveGameInstances.Find(GameInstanceId))
	{
		return *FoundState;
	}
	else
	{
		// Persistent GameInstance
		Trace::FNetProfilerGameInstanceInternal& GameInstance = NetProfilerProvider.CreateGameInstance();
		GameInstance.Instance.GameInstanceId = GameInstanceId;
		GameInstance.Instance.LifeTime.Begin = GetLastTimestamp();

		// Active GameInstanceState
		TSharedRef<FNetTraceGameInstanceState> GameInstanceState = MakeShared<FNetTraceGameInstanceState>();
		ActiveGameInstances.Add(GameInstanceId, GameInstanceState);
		GameInstanceState->GameInstanceIndex = GameInstance.Instance.GameInstanceIndex;

		return GameInstanceState;
	}
}

void FNetTraceAnalyzer::DestroyActiveGameInstanceState(uint32 GameInstanceId)
{
	if (TSharedRef<FNetTraceAnalyzer::FNetTraceGameInstanceState>* FoundState = ActiveGameInstances.Find(GameInstanceId))
	{
		// Mark as closed
		if (Trace::FNetProfilerGameInstanceInternal* GameInstance = NetProfilerProvider.EditGameInstance((*FoundState)->GameInstanceIndex))
		{
			GameInstance->Instance.LifeTime.End = GetLastTimestamp();
		}
		ActiveGameInstances.Remove(GameInstanceId);
	}
}

FNetTraceAnalyzer::FNetTraceConnectionState* FNetTraceAnalyzer::GetActiveConnectionState(uint32 GameInstanceId, uint32 ConnectionId)
{
	if (TSharedRef<FNetTraceAnalyzer::FNetTraceGameInstanceState>* FoundState = ActiveGameInstances.Find(GameInstanceId))
	{
		if (TSharedRef<FNetTraceConnectionState>* ConnectionState =  (*FoundState)->ActiveConnections.Find(ConnectionId))
		{
			return &(*ConnectionState).Get();
		}
	}
		
	return nullptr;
}
