//
// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
//

#include "Trace/Private/Event.h"

#if UE_TRACE_ENABLED

#include "Trace/Private/Field.h"
#include "Trace/Private/Writer.inl"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
namespace Private
{

template <typename Type>
struct TLateAtomic
{
	typedef TTraceAtomic<Type> InnerType;
	InnerType* operator -> () { return (InnerType*)Buffer; }
	alignas(InnerType) char Buffer[sizeof(InnerType)];
};

static TLateAtomic<uint32>	GEventUidCounter;		// = 0;
static TLateAtomic<FEvent*>	GHeadEvent;				// = nullptr;
void						Writer_Initialize();

} // namespace Private



////////////////////////////////////////////////////////////////////////////////
static uint32 GetHashImpl(const ANSICHAR* Input, uint32 Continuation=0x811c9dc5)
{
	for (; *Input; ++Input)
	{
		Continuation ^= *Input;
		Continuation *= 0x01000193;
	}
	return Continuation;
}

////////////////////////////////////////////////////////////////////////////////
static uint32 GetLoggerHash(const ANSICHAR* LoggerName)
{
	return GetHashImpl(LoggerName);
}

////////////////////////////////////////////////////////////////////////////////
static uint32 GetEventHash(const ANSICHAR* LoggerName, const ANSICHAR* EventName)
{
	uint32 Ret = GetHashImpl(LoggerName);
	Ret = GetHashImpl("@", Ret);
	Ret = GetHashImpl(EventName, Ret);
	return Ret;
}



////////////////////////////////////////////////////////////////////////////////
FEvent* FEvent::Find(const ANSICHAR* LoggerName, const ANSICHAR* EventName)
{
	uint32 EventHash = GetEventHash(LoggerName, EventName);

	FEvent* Candidate = Private::GHeadEvent->load(std::memory_order_relaxed);
	for (; Candidate != nullptr; Candidate = (FEvent*)(Candidate->Handle))
	{
		if (Candidate->Hash == EventHash)
		{
			return Candidate;
		}
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
void FEvent::Create(
	FEvent* Target,
	const FLiteralName& LoggerName,
	const FLiteralName& EventName,
	const FFieldDesc* FieldDescs,
	uint32 FieldCount)
{
	using namespace Private;

	Writer_Initialize();

	// Assign a unique ID for this event
	uint32 Uid = GEventUidCounter->fetch_add(1, std::memory_order_relaxed);
	Uid += uint32(EKnownEventUids::User);

	if (Uid >= uint32(EKnownEventUids::Max))
	{
		Target->bEnabled = false;
		Target->bInitialized = true;
		return;
	}

	// Fill out the target event's properties
	Target->LoggerHash = GetLoggerHash(LoggerName.Ptr);
	Target->Hash = GetEventHash(LoggerName.Ptr, EventName.Ptr);
 	Target->Uid = uint16(Uid);
	Target->bEnabled = true;
	Target->bInitialized = true;

	// Calculate the number of fields and size of name data.
	int NamesSize = LoggerName.Length + EventName.Length;
	for (uint32 i = 0; i < FieldCount; ++i)
	{
		NamesSize += FieldDescs[i].NameSize;
	}

	// Allocate the new event event in the log stream.
	uint16 EventSize = sizeof(FNewEventEvent);
	EventSize += sizeof(FNewEventEvent::Fields[0]) * FieldCount;
	EventSize += NamesSize;
	auto& Event = *(FNewEventEvent*)Writer_BeginLog(uint16(EKnownEventUids::NewEvent), EventSize);

	// Write event's main properties.
	Event.Uid = Target->Uid;
	Event.LoggerNameSize = LoggerName.Length;
	Event.EventNameSize = EventName.Length;

	// Write details about event's fields
	Event.FieldCount = FieldCount;
	for (uint32 i = 0; i < FieldCount; ++i)
	{
		const FFieldDesc& FieldDesc = FieldDescs[i];
		auto& Out = Event.Fields[i];
		Out.Offset = FieldDesc.ValueOffset;
		Out.Size = FieldDesc.ValueSize;
		Out.TypeInfo = FieldDesc.TypeInfo;
		Out.NameSize = FieldDesc.NameSize;
	}

	// Write names
	uint8* Cursor = (uint8*)(Event.Fields + FieldCount);
	auto WriteName = [&Cursor] (const ANSICHAR* Data, uint32 Size)
	{
		memcpy(Cursor, Data, Size);
		Cursor += Size;
	};

	WriteName(LoggerName.Ptr, LoggerName.Length);
	WriteName(EventName.Ptr, EventName.Length);
	for (uint32 i = 0; i < FieldCount; ++i)
	{
		const FFieldDesc& Desc = FieldDescs[i];
		WriteName(Desc.Name, Desc.NameSize);
	}

	Writer_EndLog(&(uint8&)Event);

	// Add this new event into the list so we can look them up later.
	while (true)
	{
		FEvent* HeadEvent = GHeadEvent->load(std::memory_order_relaxed);
		Target->Handle = HeadEvent;
		if (GHeadEvent->compare_exchange_weak(HeadEvent, Target, std::memory_order_release))
		{
			break;
		}
	}
}

} // namespace Trace

#endif // UE_TRACE_ENABLED
