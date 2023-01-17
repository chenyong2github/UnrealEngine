// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/ObjectHandleTracking.h"

#if UE_WITH_OBJECT_HANDLE_TRACKING

COREUOBJECT_API std::atomic<int32> UE::CoreUObject::Private::ObjectHandleEventIndex;
COREUOBJECT_API UE::CoreUObject::Private::FObjectHandleEvents UE::CoreUObject::Private::ObjectHandleEvents[2];

namespace UE::CoreUObject::Private
{
	FObjectHandleEvents& BeginWritingEvents()
	{
		// We only allow adding/removing the callbacks from a single thread.
		check(IsInGameThread());

		// Get what the last and new index will be.
		const int32 LastIndex = ObjectHandleEventIndex;
		const int32 NewIndex = LastIndex == 0 ? 1 : 0;

		// Make sure we're not still using the events, this should definitely not be true, because
		// we spin lock at the end of any of these functions to make sure all using has stopped.
		check(!ObjectHandleEvents[NewIndex].IsUsing());

		// We need to take whatever delegates are setup on the previous index and copy them over to new set.
		ObjectHandleEvents[NewIndex].ObjectHandleReadEvent = ObjectHandleEvents[LastIndex].ObjectHandleReadEvent;
		ObjectHandleEvents[NewIndex].ClassReferenceResolvedEvent = ObjectHandleEvents[LastIndex].ClassReferenceResolvedEvent;
		ObjectHandleEvents[NewIndex].ObjectHandleReferenceResolvedEvent = ObjectHandleEvents[LastIndex].ObjectHandleReferenceResolvedEvent;
		ObjectHandleEvents[NewIndex].ObjectHandleReferenceLoadedEvent = ObjectHandleEvents[LastIndex].ObjectHandleReferenceLoadedEvent;

		// Now toggle which buffer we're using.
		ObjectHandleEventIndex = NewIndex;

		// Ok, now we need to spin lock until nobody is using the LastIndex, this will prevent us from
		// calling this function right after this one and assuming the previous set isn't being used,
		// similarly while we were doing this, the BeginReadingEvents() function spun lock after marked
		// that they were using the event set, so now that we've set the new event index, if we spin lock
		// here, that should ensure that any existing readers finish up what they were doing.
		while (ObjectHandleEvents[LastIndex].IsUsing())
		{
			// Spin!
		}

		return ObjectHandleEvents[NewIndex];
	}
}

FDelegateHandle AddObjectHandleReadCallback(FObjectHandleReadDelegate Callback)
{
	UE::CoreUObject::Private::FObjectHandleEvents& Events = UE::CoreUObject::Private::BeginWritingEvents();
	return Events.ObjectHandleReadEvent.Add(Callback);
}

void RemoveObjectHandleReadCallback(FDelegateHandle DelegateHandle)
{
	UE::CoreUObject::Private::FObjectHandleEvents& Events = UE::CoreUObject::Private::BeginWritingEvents();
	Events.ObjectHandleReadEvent.Remove(DelegateHandle);
}

FDelegateHandle AddObjectHandleClassResolvedCallback(FObjectHandleClassResolvedDelegate Callback)
{
	UE::CoreUObject::Private::FObjectHandleEvents& Events = UE::CoreUObject::Private::BeginWritingEvents();
	return Events.ClassReferenceResolvedEvent.Add(Callback);
}

void RemoveObjectHandleClassResolvedCallback(FDelegateHandle DelegateHandle)
{
	UE::CoreUObject::Private::FObjectHandleEvents& Events = UE::CoreUObject::Private::BeginWritingEvents();
	Events.ClassReferenceResolvedEvent.Remove(DelegateHandle);
}

FDelegateHandle AddObjectHandleReferenceResolvedCallback(FObjectHandleReferenceResolvedDelegate Callback)
{
	UE::CoreUObject::Private::FObjectHandleEvents& Events = UE::CoreUObject::Private::BeginWritingEvents();
	return Events.ObjectHandleReferenceResolvedEvent.Add(Callback);
}

void RemoveObjectHandleReferenceResolvedCallback(FDelegateHandle DelegateHandle)
{
	UE::CoreUObject::Private::FObjectHandleEvents& Events = UE::CoreUObject::Private::BeginWritingEvents();
	Events.ObjectHandleReferenceResolvedEvent.Remove(DelegateHandle);
}

FDelegateHandle AddObjectHandleReferenceLoadedCallback(FObjectHandleReferenceLoadedDelegate Callback)
{
	UE::CoreUObject::Private::FObjectHandleEvents& Events = UE::CoreUObject::Private::BeginWritingEvents();
	return Events.ObjectHandleReferenceLoadedEvent.Add(Callback);
}

void RemoveObjectHandleReferenceLoadedCallback(FDelegateHandle DelegateHandle)
{
	UE::CoreUObject::Private::FObjectHandleEvents& Events = UE::CoreUObject::Private::BeginWritingEvents();
	Events.ObjectHandleReferenceLoadedEvent.Remove(DelegateHandle);
}

#endif // UE_WITH_OBJECT_HANDLE_TRACKING
