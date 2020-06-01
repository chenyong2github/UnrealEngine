// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnalyticsEventAttribute.h"
#include "Templates/Function.h"
#include "Misc/ScopeLock.h"

/**
 * Mixin class for Epic Telemetgry implementors.
 * The purpose of this class is to support the concept of caching events that are added via the standard RecordEvent API
 * and serializing them into a payload in a Json format compatible with Epic's backend data collectors.
 * The job of transporting these payloads to an external collector (generally expected to be via HTTP) is left to
 * higher level classes to implement.
 *
 * All public APIs in this class are threadsafe. Implemented via crappy critical sections for now, but they are safe. */
class ANALYTICSET_API FAnalyticsProviderETEventCache
{
public:
	FAnalyticsProviderETEventCache();

	/** This call is threadsafe (via crappy CS, but it's safe). */
	void AddToCache(FString EventName, TArray<FAnalyticsEventAttribute>&& Attributes, bool bIsJsonEvent);

	/** This call is threadsafe (via crappy CS, but it's safe). */
	void SetDefaultAttributes(TArray<FAnalyticsEventAttribute>&& DefaultAttributes);

	TArray<FAnalyticsEventAttribute> GetDefaultAttributes() const;

	/** This call is threadsafe (via crappy CS, but it's safe). */
	FString FlushCache(SIZE_T* OutEventCount = nullptr);

	/** Called by legacy provider configurations for data collectors that don't actually support caching events. This call is threadsafe (via crappy CS, but it's safe). */
	void FlushCacheLegacy(TFunctionRef<void(const FString&, const FString&)> SendPayloadFunc);

	/**
	* Determines whether we need to flush. Generally, this is only if we have cached events.
	* Since the first event is always a control event, and we overwrite multiple control events in a row,
	* we can safely say that if the array is longer than 1 item, it must have a real event in it to flush.
	*/
	bool CanFlush() const;

	/** Gets an approximate number of cached events. Implemented for speed so doesn't check every event to see if it's a new set of default attributes. Expected to be used to approximate when to flush the cache due to too many events. */
	int GetNumCachedEvents() const;

	friend class Lock;

	/** For when you need to take a lock across multiple API calls */
	class Lock
	{
	public:
		explicit Lock(FAnalyticsProviderETEventCache& EventCache)
			:ScopedLock(&EventCache.CachedEventsCS)
		{}
	private:
		FScopeLock ScopedLock;
	};

private:
	/**
	* Analytics event entry to be cached
	*/
	struct FAnalyticsEventEntry
	{
		/** name of event */
		FString EventName;
		/** optional list of attributes */
		TArray<FAnalyticsEventAttribute> Attributes;
		/** local time when event was triggered */
		FDateTime TimeStamp;
		/** Whether this event was added using the Json API. */
		uint32 bIsJsonEvent : 1;
		/** Whether this event is setting the default attributes to add to all events. Every cached event list will start with one of these, though it may be empty. */
		uint32 bIsDefaultAttributes : 1;
		/**
		* Constructor. Requires rvalue-refs to ensure we move values efficiently into this struct.
		*/
		FAnalyticsEventEntry(FString&& InEventName, TArray<FAnalyticsEventAttribute>&& InAttributes, bool bInIsJsonEvent, bool bInIsDefaultAttributes)
			: EventName(MoveTemp(InEventName))
			, Attributes(MoveTemp(InAttributes))
			, TimeStamp(FDateTime::UtcNow())
			, bIsJsonEvent(bInIsJsonEvent)
			, bIsDefaultAttributes(bInIsDefaultAttributes)
		{}
	};

	/**
	* List of analytic events pending a server update .
	* NOTE: This MUST be accessed inside a lock on CachedEventsCS!!
	*/
	TArray<FAnalyticsEventEntry> CachedEvents;

	/** Critical section for updating the CachedEvents. Mutable to allow const methods to access the list. */
	mutable FCriticalSection CachedEventsCS;
};