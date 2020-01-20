// Copyright Epic Games, Inc. All Rights Reserved.

#include "Net/Core/Trace/Reporters/NetTraceReporter.h"

#if UE_NET_TRACE_ENABLED

#include "Containers/UnrealString.h"
#include "HAL/PlatformTime.h"
#include "Net/Core/Trace/NetTrace.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Trace/Trace.h"

uint32 FNetTraceReporter::NetTraceReporterVersion = 1;

// We always output this event first to make sure we have a version number for backwards compatibility
UE_TRACE_EVENT_BEGIN(NetTrace, InitEvent, Always)
	UE_TRACE_EVENT_FIELD(uint64, Timestamp)
	UE_TRACE_EVENT_FIELD(uint32, NetTraceVersion)
	UE_TRACE_EVENT_FIELD(uint32, NetTraceReporterVersion)
UE_TRACE_EVENT_END()

// Trace a name, the utf encoded name is attached as a attachment
UE_TRACE_EVENT_BEGIN(NetTrace, NameEvent, Always)
	UE_TRACE_EVENT_FIELD(uint16, NameId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetTrace, ObjectCreatedEvent, Always)	
	UE_TRACE_EVENT_FIELD(uint64, TypeId)
	UE_TRACE_EVENT_FIELD(uint32, ObjectId)
	UE_TRACE_EVENT_FIELD(uint32, OwnerId)
	UE_TRACE_EVENT_FIELD(uint16, NameId)
	UE_TRACE_EVENT_FIELD(uint8, GameInstanceId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetTrace, ObjectDestroyedEvent, Always)
	UE_TRACE_EVENT_FIELD(uint32, ObjectId)
	UE_TRACE_EVENT_FIELD(uint8, GameInstanceId)
UE_TRACE_EVENT_END()

// What else do we want to know? should we maybe call this a connectionEvent instead?
UE_TRACE_EVENT_BEGIN(NetTrace, ConnectionCreatedEvent, Always)
	UE_TRACE_EVENT_FIELD(uint16, ConnectionId)
	UE_TRACE_EVENT_FIELD(uint8, GameInstanceId)
UE_TRACE_EVENT_END()

// Add close reason?
UE_TRACE_EVENT_BEGIN(NetTrace, ConnectionClosedEvent, Always)
	UE_TRACE_EVENT_FIELD(uint16, ConnectionId)
	UE_TRACE_EVENT_FIELD(uint8, GameInstanceId)
UE_TRACE_EVENT_END()

// rename 
UE_TRACE_EVENT_BEGIN(NetTrace, InstanceDestroyedEvent, Always)
	UE_TRACE_EVENT_FIELD(uint8, GameInstanceId)
UE_TRACE_EVENT_END()

// Packet data is transmitted as attachment
UE_TRACE_EVENT_BEGIN(NetTrace, PacketContentEvent, Always)
	UE_TRACE_EVENT_FIELD(uint16, ConnectionId)
	UE_TRACE_EVENT_FIELD(uint8, GameInstanceId)
	UE_TRACE_EVENT_FIELD(uint8, PacketType)
UE_TRACE_EVENT_END()

//$TODO: Drop the timestamp when we can get them for free on the analysis side
UE_TRACE_EVENT_BEGIN(NetTrace, PacketEvent, Always)
	UE_TRACE_EVENT_FIELD(uint64, Timestamp)
	UE_TRACE_EVENT_FIELD(uint32, PacketBits)
	UE_TRACE_EVENT_FIELD(uint32, SequenceNumber)
	UE_TRACE_EVENT_FIELD(uint16, ConnectionId)
	UE_TRACE_EVENT_FIELD(uint8, GameInstanceId)
	UE_TRACE_EVENT_FIELD(uint8, PacketType)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetTrace, PacketDroppedEvent, Always)
	UE_TRACE_EVENT_FIELD(uint64, Timestamp)
	UE_TRACE_EVENT_FIELD(uint32, SequenceNumber)
	UE_TRACE_EVENT_FIELD(uint16, ConnectionId)
	UE_TRACE_EVENT_FIELD(uint8, GameInstanceId)
	UE_TRACE_EVENT_FIELD(uint8, PacketType)
UE_TRACE_EVENT_END()

void FNetTraceReporter::ReportInitEvent(uint32 NetTraceVersion)
{
	UE_TRACE_LOG(NetTrace, InitEvent)
		<< InitEvent.Timestamp(FPlatformTime::Cycles64())
		<< InitEvent.NetTraceVersion(NetTraceVersion)
		<< InitEvent.NetTraceReporterVersion(NetTraceReporterVersion);
}

void FNetTraceReporter::ReportInstanceDestroyed(uint32 GameInstanceId)
{
	UE_TRACE_LOG(NetTrace, InstanceDestroyedEvent)
		<< InstanceDestroyedEvent.GameInstanceId(GameInstanceId);
}

void FNetTraceReporter::ReportAnsiName(FNetDebugNameId NameId, uint32 NameSize, const char* Name)
{
	UE_TRACE_LOG(NetTrace, NameEvent, NameSize)
		<< NameEvent.NameId(NameId)
		<< NameEvent.Attachment(Name, NameSize);
}

void FNetTraceReporter::ReportPacketDropped(const FNetTracePacketInfo& PacketInfo)
{
	UE_TRACE_LOG(NetTrace, PacketDroppedEvent)
		<< PacketDroppedEvent.Timestamp(FPlatformTime::Cycles64())
		<< PacketDroppedEvent.SequenceNumber(PacketInfo.PacketSequenceNumber)
		<< PacketDroppedEvent.ConnectionId(PacketInfo.ConnectionId)
		<< PacketDroppedEvent.GameInstanceId(PacketInfo.GameInstanceId)
		<< PacketDroppedEvent.PacketType((uint8)PacketInfo.PacketType);
}

void FNetTraceReporter::ReportPacket(const FNetTracePacketInfo& PacketInfo, uint32 PacketBits)
{
	UE_TRACE_LOG(NetTrace, PacketEvent)
		<< PacketEvent.Timestamp(FPlatformTime::Cycles64())
		<< PacketEvent.PacketBits(PacketBits)
		<< PacketEvent.SequenceNumber(PacketInfo.PacketSequenceNumber)
		<< PacketEvent.ConnectionId(PacketInfo.ConnectionId)
		<< PacketEvent.GameInstanceId(PacketInfo.GameInstanceId)
		<< PacketEvent.PacketType((uint8)PacketInfo.PacketType);
}

void FNetTraceReporter::ReportPacketContent(FNetTracePacketContentEvent* Events, uint32 EventCount, const FNetTracePacketInfo& PacketInfo)
{
	// $IRIS: $TODO: Get Max attachmentsize when that is available from trace system
	const uint32 BufferSize = 3096u;
	const uint32 MaxEncodedEventSize = 20u;
	const uint32 FlushBufferThreshold = BufferSize - MaxEncodedEventSize;

	uint8 Buffer[BufferSize];
	uint8* BufferPtr = Buffer;
	
	uint64 LastOffset = 0u;

	auto FlushPacketContentBuffer = [](const FNetTracePacketInfo& InPacketInfo, const uint8* InBuffer, uint32 Count)
	{
		UE_TRACE_LOG(NetTrace, PacketContentEvent, Count)
			<< PacketContentEvent.ConnectionId(InPacketInfo.ConnectionId)
			<< PacketContentEvent.GameInstanceId(InPacketInfo.GameInstanceId)
			<< PacketContentEvent.PacketType((uint8)InPacketInfo.PacketType)
			<< PacketContentEvent.Attachment(InBuffer, Count);
	};

	for (const FNetTracePacketContentEvent& CurrentEvent : MakeArrayView(Events, EventCount))
	{
		// Flush
		if ((BufferPtr - Buffer) > FlushBufferThreshold)
		{
			FlushPacketContentBuffer(PacketInfo, Buffer, BufferPtr - Buffer);
			BufferPtr = Buffer;
			LastOffset = 0;
		}

		// Encode event data to buffer

		// Type
		*(BufferPtr++) = CurrentEvent.EventType;

		switch (ENetTracePacketContentEventType(CurrentEvent.EventType))
		{
			case ENetTracePacketContentEventType::Object:
			case ENetTracePacketContentEventType::NameId:
			{
				// NestingLevel
				*(BufferPtr++) = CurrentEvent.NestingLevel;

				uint32 EventId = (ENetTracePacketContentEventType)CurrentEvent.EventType == ENetTracePacketContentEventType::Object ? CurrentEvent.ObjectId : CurrentEvent.DebugNameId;
				FTraceUtils::Encode7bit(EventId, BufferPtr);

				// Encode event data, all offsets are delta compressed against previous begin marker
				const uint64 StartPos = CurrentEvent.StartPos;

				// Start
				FTraceUtils::Encode7bit(StartPos - LastOffset, BufferPtr);
				LastOffset = StartPos;

				// End
				FTraceUtils::Encode7bit(CurrentEvent.EndPos - StartPos, BufferPtr);
			}
			break;
			case ENetTracePacketContentEventType::BunchEvent:
			{
				// DebugName
				uint32 EventId = CurrentEvent.DebugNameId;
				FTraceUtils::Encode7bit(EventId, BufferPtr);

				// Start, do not delta compress as we have to deal with overshoot of previous bunch
				FTraceUtils::Encode7bit(CurrentEvent.StartPos, BufferPtr);

				// End
				FTraceUtils::Encode7bit(CurrentEvent.EndPos - CurrentEvent.StartPos, BufferPtr);

				// Must reset LastOffest when we begin a new bunch
				LastOffset = 0U;
			}
			break;
			case ENetTracePacketContentEventType::BunchHeaderEvent:
			{
				const uint32 BunchEventCount = CurrentEvent.StartPos;
				const uint32 HeaderSize = CurrentEvent.EndPos;

				// EventCount
				FTraceUtils::Encode7bit(BunchEventCount, BufferPtr);

				// HeaderSize if any
				FTraceUtils::Encode7bit(HeaderSize, BufferPtr);

				if (HeaderSize)
				{
					FTraceUtils::Encode7bit(CurrentEvent.ChannelIndex, BufferPtr);
				}
			}
			break;
		}
	}

	if (BufferPtr > Buffer)
	{
		FlushPacketContentBuffer(PacketInfo, Buffer, BufferPtr - Buffer);
		BufferPtr = Buffer;
	}
}

void FNetTraceReporter::ReportConnectionCreated(uint32 GameInstanceId, uint32 ConnectionId)
{
	UE_TRACE_LOG(NetTrace, ConnectionCreatedEvent)
		<< ConnectionCreatedEvent.ConnectionId(ConnectionId)
		<< ConnectionCreatedEvent.GameInstanceId(GameInstanceId);
}

void FNetTraceReporter::ReportConnectionClosed(uint32 GameInstanceId, uint32 ConnectionId)
{
	UE_TRACE_LOG(NetTrace, ConnectionClosedEvent)
		<< ConnectionClosedEvent.ConnectionId(ConnectionId)
		<< ConnectionClosedEvent.GameInstanceId(GameInstanceId);
}

void FNetTraceReporter::ReportObjectCreated(uint32 GameInstanceId, uint32 NetObjectId, FNetDebugNameId NameId, uint64 TypeIdentifier, uint32 OwnerId)
{
	UE_TRACE_LOG(NetTrace, ObjectCreatedEvent)
		<< ObjectCreatedEvent.TypeId(TypeIdentifier)
		<< ObjectCreatedEvent.ObjectId(NetObjectId)
		<< ObjectCreatedEvent.OwnerId(OwnerId)
		<< ObjectCreatedEvent.NameId(NameId)
		<< ObjectCreatedEvent.GameInstanceId(GameInstanceId);
}

void FNetTraceReporter::ReportObjectDestroyed(uint32 GameInstanceId, uint32 NetObjectId)
{
	UE_TRACE_LOG(NetTrace, ObjectDestroyedEvent)
		<< ObjectDestroyedEvent.ObjectId(NetObjectId)
		<< ObjectDestroyedEvent.GameInstanceId(GameInstanceId);
}

#endif
