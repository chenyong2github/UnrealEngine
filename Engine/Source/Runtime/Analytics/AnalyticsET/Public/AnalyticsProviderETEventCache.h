// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnalyticsEventAttribute.h"
#include "Templates/Function.h"
#include "Misc/ScopeLock.h"

/**
 * Mixin class for Epic Telemetry implementors.
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
	UE_DEPRECATED(4.25, "This method has been deprecated. Use FJsonFragment to construct Json attributes instead, or call the version that doesn't take a bIsJsonEvent argument.")
	void AddToCache(FString EventName, TArray<FAnalyticsEventAttribute>&& Attributes, bool bIsJsonEvent);
	void AddToCache(FString EventName, TArray<FAnalyticsEventAttribute>&& Attributes);
	void AddToCache(FString EventName);

	/**
	 * Sets an array of attributes that will automatically be appended to any event that is sent.
	 * Logical effect is like adding them to all events before calling RecordEvent.
	 * Practically, it is implemented much more efficiently from a storage and allocation perspective.
	 * This call is threadsafe (via crappy CS, but it's safe).
	 */
	void SetDefaultAttributes(TArray<FAnalyticsEventAttribute>&& DefaultAttributes);

	/**
	 * @return the current array of default attributes.
	 */
	TArray<FAnalyticsEventAttribute> GetDefaultAttributes() const;

	/**
	 * @return the number of default attributes are currently being applied.
	 */
	int32 GetDefaultAttributeCount() const;

	/**
	 * Range checking is not done, similar to TArray. Use GetDefaultAttributeCount() first!
	 * @return one attribute of the default attributes so we don't have to copy the entire attribute array.
	 */
	FAnalyticsEventAttribute GetDefaultAttribute(int32 AttributeIndex) const;

	/** This call is threadsafe (via crappy CS, but it's safe). */
	FString FlushCache(SIZE_T* OutEventCount = nullptr);

	/** Called by legacy provider configurations for data collectors that don't actually support caching events. This call is threadsafe (via crappy CS, but it's safe). */
	void FlushCacheLegacy(TFunctionRef<void(const FString&, const FString&)> SendPayloadFunc);

	/**
	* Determines whether we need to flush. Generally, this is only if we have cached events.
	* Legacy method. This essentially returns GetNumCachedEvents() > 0
	*/
	bool CanFlush() const;

	/** Gets the number of cached events. Expected to be used to approximate when to flush the cache due to too many events. */
	int GetNumCachedEvents() const;

	/** Computes the approximate serialized number of bytes for this event. Used to help caching schemes flush when payloads reach a certain size. */
	int ComputeApproximateEventChars(const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attributes) const;

	/** Computes an approximate size of the payload so far if it were flushed right now. Used to help caching schemes flush when payloads reach a certain size. */
	int ComputeApproximatePayloadChars() const;

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
		/** Whether this event is setting the default attributes to add to all events. Every cached event list will start with one of these, though it may be empty. */
		uint32 bIsDefaultAttributes : 1;
		/**
		* Constructor. Requires rvalue-refs to ensure we move values efficiently into this struct.
		*/
		FAnalyticsEventEntry(FString&& InEventName, TArray<FAnalyticsEventAttribute>&& InAttributes, bool bInIsDefaultAttributes)
			: EventName(MoveTemp(InEventName))
			, Attributes(MoveTemp(InAttributes))
			, TimeStamp(FDateTime::UtcNow())
			, bIsDefaultAttributes(bInIsDefaultAttributes)
		{}
	};

	/**
	* List of analytic events pending a server update .
	* NOTE: This MUST be accessed inside a lock on CachedEventsCS!!
	*/
	TArray<FAnalyticsEventEntry> CachedEvents;

	int EventSizeEstimate = 0;
	int NumEventsCached = 0;
	int CurrentDefaultAttributeSizeEstimate = 0;

	/** Critical section for updating the CachedEvents. Mutable to allow const methods to access the list. */
	mutable FCriticalSection CachedEventsCS;
};