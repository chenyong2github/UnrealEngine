// Copyright Epic Games, Inc. All Rights Reserved.
#include "NetTraceAnalyzer.h"
#include "AnalysisServicePrivate.h"
#include "Common/Utils.h"
#include "TraceServices/Model/Threads.h"
#include "Logging/LogMacros.h"

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

bool FNetTraceAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
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
			// we always trace the version so that we make sure that we are backwards compatible with older trace stream
			NetTraceVersion = EventData.GetValue<uint32>("NetTraceVersion");
			NetTraceReporterVersion = EventData.GetValue<uint32>("NetTraceReporterVersion");
			const uint64 TimestampCycles = EventData.GetValue<uint64>("Timestamp");

			LastTimeStamp = Context.SessionContext.TimestampFromCycle(TimestampCycles);

			NetProfilerProvider.SetNetTraceVersion(NetTraceVersion);
		}
		break;

		case RouteId_InstanceDestroyedEvent:
		{
			const uint8 GameInstanceId = EventData.GetValue<uint8>("ReplicationSystemId");
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
			const uint16 ConnectionId = EventData.GetValue<uint16>("ConnectionId");
			const uint8 GameInstanceId = EventData.GetValue<uint8>("ReplicationSystemId");
			const uint8 PacketType =  EventData.GetValue<uint8>("PacketType");

			TSharedRef<FNetTraceGameInstanceState> GameInstanceState = GetOrCreateActiveGameInstanceState(GameInstanceId);
			TSharedRef<FNetTraceConnectionState> ConnectionState = GetActiveConnectionState(GameInstanceId, ConnectionId);

			Trace::ENetProfilerConnectionMode ConnectionMode = Trace::ENetProfilerConnectionMode(PacketType);

			Trace::FNetProfilerConnectionData& ConnectionData = NetProfilerProvider.EditConnectionData(ConnectionState->ConnectionIndex, ConnectionMode);
			++ConnectionData.ContentEventChangeCount;

			TPagedArray<Trace::FNetProfilerContentEvent>& Events = ConnectionData.ContentEvents;

			// New Packet, update state
			if (ConnectionState->CurrentPacketStartIndex[ConnectionMode] == Events.Num())
			{
				ConnectionState->ConnectionMode = ConnectionMode;
			}
			else
			{
				// Corrupt data
				check(ConnectionState->CurrentPacketStartIndex[ConnectionMode] == Events.Num() || (ConnectionState->ConnectionMode == ConnectionMode));
			}

			// Decode batched events
			uint64 BufferSize = EventData.GetAttachmentSize();
			const uint8* BufferPtr = EventData.GetAttachment();
			const uint8* BufferEnd = BufferPtr + BufferSize;
			uint64 LastOffset = 0;

			while (BufferPtr < BufferEnd)
			{
				// Decode data
				const uint64 DecodedNameOrObjectId = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
				const uint64 DecodedEventStartPos = FTraceAnalyzerUtils::Decode7bit(BufferPtr) + LastOffset;
				LastOffset = DecodedEventStartPos;
				const uint64 DecodedEventEndPos = FTraceAnalyzerUtils::Decode7bit(BufferPtr) + DecodedEventStartPos;
				const uint8 DecodedEventType = *BufferPtr++;
				const uint8 DecodedNestingLevel = *BufferPtr++;

				// Store the event, should we store it immediately? probably.
				Trace::FNetProfilerContentEvent& Event = Events.PushBack();

				Event.StartPos = DecodedEventStartPos;
				Event.EndPos = DecodedEventEndPos;
				Event.Level = DecodedNestingLevel;

				Event.ObjectInstanceIndex = 0;
				Event.NameIndex = 0;

				if (DecodedEventType == 0)
				{
					// Object index, need to lookup name indirectly
					if (const FNetTraceActiveObjectState* ActiveObjectState = GameInstanceState->ActiveObjects.Find(DecodedNameOrObjectId))
					{
						Event.NameIndex = ActiveObjectState->NameIndex;
						Event.ObjectInstanceIndex = ActiveObjectState->ObjectIndex;
					}
				}
				else if (DecodedEventType == 1)
				{
					if (const uint32* NetProfilerNameIndex = TracedNameIdToNetProfilerNameIdMap.Find(DecodedNameOrObjectId))
					{
						Event.NameIndex = *NetProfilerNameIndex;
					}
				}

				// EventTypeIndex does not match NameIndex as we might see the same name on different levels
				Event.EventTypeIndex = GetTracedEventTypeIndex(Event.NameIndex, Event.Level);
			}
			check(BufferPtr == BufferEnd);
		}
		break;

		case RouteId_PacketEvent:
		{
			const uint64 TimestampCycles = EventData.GetValue<uint64>("Timestamp");
			const uint32 SequenceNumber = EventData.GetValue<uint32>("SequenceNumber");
			const uint8 GameInstanceId = EventData.GetValue<uint8>("ReplicationSystemId");
			const uint16 ConnectionId = EventData.GetValue<uint16>("ConnectionId");

			// Update LastTimestamp, later on we will be able to get timestamps piggybacked from other analyzers
			LastTimeStamp = Context.SessionContext.TimestampFromCycle(TimestampCycles);

			TSharedRef<FNetTraceGameInstanceState> GameInstanceState = GetOrCreateActiveGameInstanceState(GameInstanceId);
			TSharedRef<FNetTraceConnectionState> ConnectionState = GetActiveConnectionState(GameInstanceId, ConnectionId);

			// Add the packet
			Trace::FNetProfilerConnectionData& ConnectionData = NetProfilerProvider.EditConnectionData(ConnectionState->ConnectionIndex, ConnectionState->ConnectionMode);
			Trace::FNetProfilerPacket& Packet = ConnectionData.Packets.PushBack();
			++ConnectionData.PacketChangeCount;

			// Fill in packet data a packet has at least 1 event
			Packet.StartEventIndex = ConnectionState->CurrentPacketStartIndex[ConnectionState->ConnectionMode];
			Packet.EventCount = ConnectionData.ContentEvents.Num() - Packet.StartEventIndex;
			Packet.TimeStamp = GetLastTimestamp();
			Packet.SequenceNumber = SequenceNumber;
			Packet.DeliveryStatus = Trace::ENetProfilerDeliveryStatus::Unknown;
			Packet.ContentSizeInBits = Packet.EventCount ? ConnectionData.ContentEvents[Packet.StartEventIndex + Packet.EventCount - 1].EndPos : 0u;
			Packet.TotalPacketSizeInBytes = (Packet.ContentSizeInBits + 7u) >> 3u;
			Packet.DeliveryStatus = Trace::ENetProfilerDeliveryStatus::Delivered;

			// Finalize received packet data and update data in the NetProfilerProvider
			//NetProfilerProvider->FinalizePacket();

			// Mark the beginning of a new packet
			ConnectionState->CurrentPacketStartIndex[ConnectionState->ConnectionMode] = ConnectionData.ContentEvents.Num();
		}
		break;

		case RouteId_PacketDroppedEvent:
		{
			const uint64 TimestampCycles = EventData.GetValue<uint64>("Timestamp");
			const uint32 SequenceNumber = EventData.GetValue<uint32>("SequenceNumber");
			const uint8 GameInstanceId = EventData.GetValue<uint8>("ReplicationSystemId");
			const uint16 ConnectionId = EventData.GetValue<uint16>("ConnectionId");
			const uint8 PacketType = EventData.GetValue<uint8>("PacketType");

			// Update LastTimestamp, later on we will be able to get timestamps piggybacked from other analyzers
			LastTimeStamp = Context.SessionContext.TimestampFromCycle(TimestampCycles);

			TSharedRef<FNetTraceGameInstanceState> GameInstanceState = GetOrCreateActiveGameInstanceState(GameInstanceId);
			TSharedRef<FNetTraceConnectionState> ConnectionState = GetActiveConnectionState(GameInstanceId, ConnectionId);

			Trace::FNetProfilerConnectionData& ConnectionData = NetProfilerProvider.EditConnectionData(ConnectionState->ConnectionIndex, Trace::ENetProfilerConnectionMode(PacketType));

			// Update packet delivery status
			NetProfilerProvider.EditPacketDeliveryStatus(ConnectionState->ConnectionIndex, Trace::ENetProfilerConnectionMode(PacketType), SequenceNumber, Trace::ENetProfilerDeliveryStatus::Dropped);
		}
		break;

		case RouteId_ConnectionCreatedEvent:
		{
			const uint8 GameInstanceId = EventData.GetValue<uint8>("ReplicationSystemId");
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
		}
		break;

		case RouteId_ConnectionClosedEvent:
		{
			const uint8 GameInstanceId = EventData.GetValue<uint8>("ReplicationSystemId");
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
		break;

		case RouteId_ObjectCreatedEvent:
		{
			const uint64 TypeId = EventData.GetValue<uint64>("TypeId");
			const uint32 ObjectId = EventData.GetValue<uint32>("ObjectId");
			const uint32 OwnerId = EventData.GetValue<uint32>("OwnerId");
			const uint16 NameId = EventData.GetValue<uint16>("NameId");
			const uint8 GameInstanceId = EventData.GetValue<uint8>("ReplicationSystemId");
			const uint16 ConnectionId = EventData.GetValue<uint16>("ConnectionId");

			TSharedRef<FNetTraceGameInstanceState> GameInstanceState = GetOrCreateActiveGameInstanceState(GameInstanceId);
			if (GameInstanceState->ActiveObjects.Contains(ObjectId))
			{
				// Verify that this is a reference that has been updated to a replicated object
				Trace::FNetProfilerObjectInstance& ObjectInstance = *NetProfilerProvider.EditObject(GameInstanceState->GameInstanceIndex, GameInstanceState->ActiveObjects[ObjectId].ObjectIndex);
				
				// Update data 
				ObjectInstance.LifeTime.Begin =  GetLastTimestamp();
				const uint32* NetProfilerNameIndex = TracedNameIdToNetProfilerNameIdMap.Find(NameId);
				ObjectInstance.NameIndex = NetProfilerNameIndex ? *NetProfilerNameIndex : 0u;
				ObjectInstance.NetId = ObjectId;
				ObjectInstance.TypeId = TypeId;
			}
			else
			{
				// Add persistent object representation
				Trace::FNetProfilerObjectInstance& ObjectInstance = NetProfilerProvider.CreateObject(GameInstanceState->GameInstanceIndex);

				// Fill in object data
				ObjectInstance.LifeTime.Begin =  GetLastTimestamp();
				const uint32* NetProfilerNameIndex = TracedNameIdToNetProfilerNameIdMap.Find(NameId);
				ObjectInstance.NameIndex = NetProfilerNameIndex ? *NetProfilerNameIndex : 0u;
				ObjectInstance.NetId = ObjectId;
				ObjectInstance.TypeId = TypeId;

				// Add to active objects
				GameInstanceState->ActiveObjects.Add(ObjectId, { ObjectInstance.ObjectIndex, ObjectInstance.NameIndex });
			}
		}
		break;

		case RouteId_ObjectDestroyedEvent:
		{
			// Remove from active instances and mark the end timestamp in the persistent instance list
			const uint8 GameInstanceId = EventData.GetValue<uint8>("ReplicationSystemId");
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
		break;
	}

	return true;
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

TSharedRef<FNetTraceAnalyzer::FNetTraceConnectionState> FNetTraceAnalyzer::GetActiveConnectionState(uint32 GameInstanceId, uint32 ConnectionId)
{
	TSharedRef<FNetTraceGameInstanceState> GameInstanceState = GetOrCreateActiveGameInstanceState(GameInstanceId);

	return GameInstanceState->ActiveConnections.FindChecked(ConnectionId);
}
