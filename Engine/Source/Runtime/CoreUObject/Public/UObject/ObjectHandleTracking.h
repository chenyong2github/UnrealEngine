// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Delegates/Delegate.h"
#include "UObject/ObjectHandleDefines.h"

struct FObjectRef;

/**
 * FObjectHandles can optionally support tracking.  Because of the low level nature of object handles, anything that
 * registers itself for these callbacks should ensure that it is:
 * 1) error free (ie: should not cause exceptions even in unusual circumstances)
 * 2) fault tolerant (ie: could be called at a time when an exception has happened)
 * 3) thread-safe (ie: could be called from any thread)
 * 4) high performance (ie: will be called many times)
 */
#if UE_WITH_OBJECT_HANDLE_TRACKING

 /**
  * Callback notifying when an object value is read from a handle.  Fired regardless of whether the handle
  * was resolved as part of the read operation or not and whether the object being read is null or not.
  *
  * @param ReadObject	The object that was read from a handle.
  */
DECLARE_DELEGATE_OneParam(FObjectHandleReadDelegate, UObject* ReadObject);

/**
 * Callback notifying when a class is resolved from an object handle or object reference.
 * Classes are resolved either independently for a given handle/reference or as part of each object resolve.
 *
 * @param SourceRef		The object reference (either user provided or internally computed from a handle) from which the class was resolved.
 * @param ClassPackage	The package containing the resolved class.
 * @param Class			The resolved class.
 */
DECLARE_DELEGATE_ThreeParams(FObjectHandleClassResolvedDelegate, const FObjectRef& SourceRef, UPackage* ObjectPackage, UClass* Class);

/**
 * Callback notifying when a object handle is resolved.
 *
 * @param SourceRef		The object reference (either user provided or internally computed from a handle) from which the class was resolved.
 * @param ClassPackage	The package containing the resolved class.
 * @param Class			The resolved class.
 */
DECLARE_DELEGATE_ThreeParams(FObjectHandleReferenceResolvedDelegate, const FObjectRef& SourceRef, UPackage* ObjectPackage, UObject* Object);

/**
 * Callback notifying when an object was loaded through an object handle.  Will not notify you about global object loads, just ones that occur
 * as the byproduct of resolving an ObjectHandle.
 *
 * @param SourceRef		The object reference (either user provided or internally computed from a handle) from which the class was resolved.
 * @param ClassPackage	The package containing the resolved class.
 * @param Class			The resolved class.
 */
DECLARE_DELEGATE_ThreeParams(FObjectHandleReferenceLoadedDelegate, const FObjectRef& SourceRef, UPackage* ObjectPackage, UObject* Object);

/**
 * Installs a new callback for notifications that an object value has been read from a handle.
 *
 * @param Function		The new handle read callback to install.
 * @return				The DelegateHandle so that you can remove the callback at a later date.
 */
COREUOBJECT_API FDelegateHandle AddObjectHandleReadCallback(FObjectHandleReadDelegate Delegate);
COREUOBJECT_API void RemoveObjectHandleReadCallback(FDelegateHandle DelegateHandle);

/**
 * Installs a new callback for notifications that a class has been resolved from an object handle or object reference.
 *
 * @param Function		The new class resolved callback to install.
 * @return				The DelegateHandle so that you can remove the callback at a later date.
 */
COREUOBJECT_API FDelegateHandle AddObjectHandleClassResolvedCallback(FObjectHandleClassResolvedDelegate Callback);
COREUOBJECT_API void RemoveObjectHandleClassResolvedCallback(FDelegateHandle DelegateHandle);

/**
 * Installs a new callback for notifications that an object has been resolved from an object handle or object reference.
 *
 * @param Function		The new object resolved callback to install.
 * @return				The DelegateHandle so that you can remove the callback at a later date.
 */
COREUOBJECT_API FDelegateHandle AddObjectHandleReferenceResolvedCallback(FObjectHandleReferenceResolvedDelegate Callback);
COREUOBJECT_API void RemoveObjectHandleReferenceResolvedCallback(FDelegateHandle DelegateHandle);

/**
 * Installs a new callback for notifications that an object has been loaded from an object handle or object reference.
 *
 * @param Function		The new object resolved callback to install.
 * @return				The DelegateHandle so that you can remove the callback at a later date.
 */
COREUOBJECT_API FDelegateHandle AddObjectHandleReferenceLoadedCallback(FObjectHandleReferenceLoadedDelegate Callback);
COREUOBJECT_API void RemoveObjectHandleReferenceLoadedCallback(FDelegateHandle DelegateHandle);


namespace UE::CoreUObject::Private
{
	DECLARE_MULTICAST_DELEGATE_OneParam(FObjectHandleReadEvent, UObject* Object);
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FClassReferenceResolvedEvent, const FObjectRef& ObjectRef, UPackage* Package, UClass* Class);
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FObjectHandleReferenceResolvedEvent, const FObjectRef& SourceRef, UPackage* ObjectPackage, UObject* Object);
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FObjectHandleReferenceLoadedEvent, const FObjectRef& SourceRef, UPackage* ObjectPackage, UObject* Object);

	struct FObjectHandleEvents
	{
		FObjectHandleReadEvent ObjectHandleReadEvent;
		FClassReferenceResolvedEvent ClassReferenceResolvedEvent;
		FObjectHandleReferenceResolvedEvent ObjectHandleReferenceResolvedEvent;
		FObjectHandleReferenceLoadedEvent ObjectHandleReferenceLoadedEvent;

		inline void BeginUsing() { UsingCount++; }
		inline void EndUsing() { --UsingCount; }
		inline bool IsUsing() const { return UsingCount > 0; }

	private:
		std::atomic<int32> UsingCount;
	};

	extern COREUOBJECT_API std::atomic<int32> ObjectHandleEventIndex;
	extern COREUOBJECT_API FObjectHandleEvents ObjectHandleEvents[2];

	inline FObjectHandleEvents& BeginReadingEvents()
	{
		// Quick spin lock to try and begin using the events, normally this wont actually spin, in like 99.999% of the time.
		while (true)
		{
			// Start by getting the current event index from the double buffer.
			const int32 InitialEventIndex = ObjectHandleEventIndex;
			// Grab the event set at that index.
			FObjectHandleEvents& Events = ObjectHandleEvents[InitialEventIndex];
			// Begin using the events, this will signal we're using them.
			Events.BeginUsing();
			// Ok - now we're going to check that we're *still* using the same index.  If we are
			// then we know that it didn't change out from under us in between getting Events, and calling BeginUsing().
			if (InitialEventIndex == ObjectHandleEventIndex)
			{
				return Events;
			}
			// If the check above failed, then we need to cease using the events, because we're about to try this again.
			Events.EndUsing();
		}
	}

	inline void OnHandleRead(UObject* Object)
	{
		FObjectHandleEvents& Events = BeginReadingEvents();
		Events.ObjectHandleReadEvent.Broadcast(Object);
		Events.EndUsing();
	}

	inline void OnClassReferenceResolved(const FObjectRef& ObjectRef, UPackage* Package, UClass* Class)
	{
		FObjectHandleEvents& Events = BeginReadingEvents();
		Events.ClassReferenceResolvedEvent.Broadcast(ObjectRef, Package, Class);
		Events.EndUsing();
	}

	inline void OnReferenceResolved(const FObjectRef& ObjectRef, UPackage* Package, UObject* Object)
	{
		FObjectHandleEvents& Events = BeginReadingEvents();
		Events.ObjectHandleReferenceResolvedEvent.Broadcast(ObjectRef, Package, Object);
		Events.EndUsing();
	}

	inline void OnReferenceLoaded(const FObjectRef& ObjectRef, UPackage* Package, UObject* Object)
	{
		FObjectHandleEvents& Events = BeginReadingEvents();
		Events.ObjectHandleReferenceLoadedEvent.Broadcast(ObjectRef, Package, Object);
		Events.EndUsing();
	}

}
#else

namespace UE::CoreUObject::Private
{
	inline void OnHandleRead(UObject* Object) { }
	inline void OnClassReferenceResolved(const FObjectRef& ObjectRef, UPackage* Package, UClass* Class) { }
	inline void OnReferenceResolved(const FObjectRef& ObjectRef, UPackage* Package, UClass* Class) { }
	inline void OnReferenceLoaded(const FObjectRef& ObjectRef, UPackage* Package, UObject* Object) { }
}


#endif