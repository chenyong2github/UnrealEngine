// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnalyticsProviderETEventCache.h"
#include "IAnalyticsProviderET.h"
#include "Analytics.h"
#include "Misc/ScopeLock.h"
#include "PlatformHttp.h"
#include "Algo/Accumulate.h"
#include "Serialization/JsonWriter.h"

namespace EventCacheStatic
{
	/** Used for testing below to ensure stable output */
	bool bUseZeroDateOffset = false;

	struct Tests
	{
		Tests()
		{
			TGuardValue<bool> GuardTestSetting(bUseZeroDateOffset, true);

			FAnalyticsProviderETEventCache cache;
			cache.AddToCache(TEXT("BasicStrings"), MakeAnalyticsEventAttributeArray(
				TEXT("ConstantStringAttribute"), TEXT("ConstantStringValue"),
				TEXT("FStringStringAttribute"), FString(TEXT("FStringValue"))
			));

			cache.AddToCache(FString(TEXT("NumericalAttributes")), MakeAnalyticsEventAttributeArray(
				TEXT("IntAttr"), MIN_int32,
				TEXT("LongAttr"), MIN_int64,
				TEXT("UIntAttr"), MAX_uint32,
				TEXT("ULongAttr"), MAX_uint64,
				TEXT("FloatAttr"), MAX_flt,
				TEXT("DoubleAttr"), MAX_dbl,
				TEXT("IntAttr2"), 0,
				TEXT("FloatAttr2"), 0.0f,
				TEXT("DoubleAttr2"), 0.0,
				TEXT("BoolTrueAttr"), true,
				TEXT("BoolFalseAttr"), false
			));
			cache.AddToCache(FString(TEXT("JsonAttributes")), MakeAnalyticsEventAttributeArray
			(
				TEXT("NullAttr"), FJsonNull(),
				TEXT("FragmentAttr"), FJsonFragment(TEXT("{\"Key\":\"Value\",\"Key2\":\"Value2\"}"))
			));

			int32 ApproxSize = cache.ComputeApproximatePayloadChars();
			FString Payload = cache.FlushCache();
			check(Payload == TEXT("{\"Events\":[{\"EventName\":\"BasicStrings\",\"DateOffset\":\"+00:00:00.000\",\"ConstantStringAttribute\":\"ConstantStringValue\",\"FStringStringAttribute\":\"FStringValue\"},{\"EventName\":\"NumericalAttributes\",\"DateOffset\":\"+00:00:00.000\",\"IntAttr\":-2147483648,\"LongAttr\":-9223372036854775808,\"UIntAttr\":4294967295,\"ULongAttr\":18446744073709551615,\"FloatAttr\":340282346638528859811704183484516925440.0,\"DoubleAttr\":179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368.0,\"IntAttr2\":0,\"FloatAttr2\":0.0,\"DoubleAttr2\":0.0,\"BoolTrueAttr\":true,\"BoolFalseAttr\":false},{\"EventName\":\"JsonAttributes\",\"DateOffset\":\"+00:00:00.000\",\"NullAttr\":null,\"FragmentAttr\":{\"Key\":\"Value\",\"Key2\":\"Value2\"}}]}"));
		}
	};

	Tests GTests;


	int ComputeAttributeSize(const FAnalyticsEventAttribute& Attribute)
	{
		return 
		// "              Name             "   :             Value              ,   (maybequoted)                                          
		   1 + Attribute.GetName().Len() + 1 + 1 + Attribute.GetValue().Len() + 1 + (Attribute.IsJsonFragment() ? 0 : 2);
	}

	int ComputeAttributeSize(const TArray<FAnalyticsEventAttribute>& Attributes)
	{
		//int Accum = 0;
		//for (const FAnalyticsEventAttribute& Attr : Attributes)
		//{
		//	Accum += EventCacheStatic::ComputeAttributeSize(Attr);
		//}
		//return Accum;
		return Algo::Accumulate(Attributes, 0, [](int Accum, const FAnalyticsEventAttribute& Attr) { return Accum + EventCacheStatic::ComputeAttributeSize(Attr); });
	}

	int ComputeEventSize(const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attributes, int CurrentDefaultAttributeSizeEstimate)
	{
		return
			// "EventName":"   EVENT_NAME     ",
					 13 +   EventName.Len() + 2
			// "DateOffset":"+00:00:00.000",
						 + 29
			// ATTRIBUTES_SIZE
			+ CurrentDefaultAttributeSizeEstimate
			// ATTRIBUTES_SIZE
			+ ComputeAttributeSize(Attributes)
			// Last attribute will not have a comma, so subtract that off the estimate.
			-1
			;

	}

	int ComputePayloadSize(int NumEventsCached, int EventSizeEstimate)
	{
		// Payload is {"Events":[{EVENT_ESTIMATE},{EVENT_ESTIMATE}]}
		// That is 13 bytes constant overhead, and 3 more bytes per event for the object bracket and comma (minus 1 for the trailing comma removal)
		return 13 + FMath::Max(0, 3 * NumEventsCached - 1) + EventSizeEstimate;
	}
}

FAnalyticsProviderETEventCache::FAnalyticsProviderETEventCache()
{
	// if we are caching events, presize the array to max size. Otherwise, we will never have more than two entries in the array (one for the default attributes, one for the actual event)
	CachedEvents.Reserve(2);
	// make sure that we always start with one control event in the CachedEvents array.
	CachedEvents.Emplace(FString(), TArray<FAnalyticsEventAttribute>(), true);
}

void FAnalyticsProviderETEventCache::AddToCache(FString EventName, TArray<FAnalyticsEventAttribute>&& Attributes, bool bIsJsonEvent)
{
	// call deprecated functions here to convert these attributes into JsonFragments, so turn off warnings.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	for (FAnalyticsEventAttribute& Attr : Attributes)
	{
		Attr.SwitchToJsonFragment();
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	AddToCache(MoveTemp(EventName), MoveTemp(Attributes));
}

void FAnalyticsProviderETEventCache::AddToCache(FString EventName, TArray<FAnalyticsEventAttribute>&& Attributes)
{
	FScopeLock ScopedLock(&CachedEventsCS);
	EventSizeEstimate += ComputeApproximateEventChars(EventName, Attributes);
	NumEventsCached++;
	CachedEvents.Emplace(MoveTemp(EventName), MoveTemp(Attributes), false);
}

void FAnalyticsProviderETEventCache::AddToCache(FString EventName)
{
	AddToCache(MoveTemp(EventName), TArray<FAnalyticsEventAttribute>());
}

void FAnalyticsProviderETEventCache::SetDefaultAttributes(TArray<FAnalyticsEventAttribute>&& DefaultAttributes)
{
	FScopeLock ScopedLock(&CachedEventsCS);

	// further events will add this many additional bytes
	// don't need to hold the lock to compute this value.
	CurrentDefaultAttributeSizeEstimate = EventCacheStatic::ComputeAttributeSize(DefaultAttributes);

	// we know we always have one entry in CachedEvents, so no need to check for Num() > 0.
	// If we are trying to add two default attribute events in a row, just overwrite the last one.
	if (CachedEvents.Last().bIsDefaultAttributes)
	{
		CachedEvents.Last() = FAnalyticsEventEntry(FString(), MoveTemp(DefaultAttributes), true);
	}
	else
	{
		CachedEvents.Emplace(FString(), MoveTemp(DefaultAttributes), true);
	}
}

TArray<FAnalyticsEventAttribute> FAnalyticsProviderETEventCache::GetDefaultAttributes() const
{
	FScopeLock ScopedLock(&CachedEventsCS);

	int32 DefaultIndex = CachedEvents.FindLastByPredicate([](const FAnalyticsEventEntry& Entry) { return Entry.bIsDefaultAttributes == 1; });
	checkf(DefaultIndex != INDEX_NONE, TEXT("failed to find default attributes entry in analytics cached events list"));
	return CachedEvents[DefaultIndex].Attributes;
}

int32 FAnalyticsProviderETEventCache::GetDefaultAttributeCount() const
{
	FScopeLock ScopedLock(&CachedEventsCS);

	int32 DefaultIndex = CachedEvents.FindLastByPredicate([](const FAnalyticsEventEntry& Entry) { return Entry.bIsDefaultAttributes == 1; });
	checkf(DefaultIndex != INDEX_NONE, TEXT("failed to find default attributes entry in analytics cached events list"));
	return CachedEvents[DefaultIndex].Attributes.Num();
}

FAnalyticsEventAttribute FAnalyticsProviderETEventCache::GetDefaultAttribute(int32 AttributeIndex) const
{
	FScopeLock ScopedLock(&CachedEventsCS);

	int32 DefaultIndex = CachedEvents.FindLastByPredicate([](const FAnalyticsEventEntry& Entry) { return Entry.bIsDefaultAttributes == 1; });
	checkf(DefaultIndex != INDEX_NONE, TEXT("failed to find default attributes entry in analytics cached events list"));
	return CachedEvents[DefaultIndex].Attributes[AttributeIndex];
}

FString FAnalyticsProviderETEventCache::FlushCache(SIZE_T* OutEventCount)
{
	FDateTime CurrentTime = FDateTime::UtcNow();

	// Track the current set of default attributes. We move into this array instead of just referencing it
	// because at the end we will push the latest value back onto the list of cached events.
	// We can do this without actually copying the array this way.
	TArray<FAnalyticsEventAttribute> CurrentDefaultAttributes;
	
	// allocate enough space for the event.
	FString Payload;
	// Reserve a bit more space for the payload in case we have to escape a lot of Json
	const int PayloadSize = ComputeApproximatePayloadChars();
	Payload.Reserve(PayloadSize + 100);
	// Avoid using the generally slow JsonWriter library, build the Json manually.
	// **** WARNING: If you change these hardcoded values, you MUST also change ComputeEventSize() helper function!!! *****
	Payload += TEXT("{\"Events\":[");

	FScopeLock ScopedLock(&CachedEventsCS);
	bool bFirstEvent = true;
	for (FAnalyticsEventEntry& Entry : CachedEvents)
	{
		if (Entry.bIsDefaultAttributes)
		{
			// This is the default attributes, so update the array.
			CurrentDefaultAttributes = MoveTemp(Entry.Attributes);
		}
		else
		{
			if (OutEventCount)
			{
				++(*OutEventCount);
			}

			// event entry
			if (bFirstEvent)
			{
				bFirstEvent = false;
			}
			else
			{
				Payload += TEXT(',');
			}

			// **** WARNING: If you change these hardcoded values, you MUST also change ComputeEventSize() helper function!!! *****
			Payload += TEXT("{\"EventName\":"); AppendEscapeJsonString(Payload, Entry.EventName);
			FString DateOffset = EventCacheStatic::bUseZeroDateOffset ? FTimespan::Zero().ToString() : (CurrentTime - Entry.TimeStamp).ToString();
			Payload += TEXT(",\"DateOffset\":"); AppendEscapeJsonString(Payload, DateOffset);

			// default attributes for this event
			for (const FAnalyticsEventAttribute& Attr : CurrentDefaultAttributes)
			{
				Payload += TEXT(',');
				AppendEscapeJsonString(Payload, Attr.GetName());
				Payload += TEXT(':');
				if (Attr.IsJsonFragment())
				{
					Payload += Attr.GetValue();
				}
				else
				{
					AppendEscapeJsonString(Payload, Attr.GetValue());
				}
			}
			// optional attributes for this event
			for (const FAnalyticsEventAttribute& Attr : Entry.Attributes)
			{
				Payload += TEXT(',');
				AppendEscapeJsonString(Payload, Attr.GetName());
				Payload += TEXT(':');
				if (Attr.IsJsonFragment())
				{
					Payload += Attr.GetValue();
				}
				else
				{
					AppendEscapeJsonString(Payload, Attr.GetValue());
				}
			}
			Payload += TEXT('}');
		}
	}

	Payload += TEXT("]}");
	if (Payload.Len() > PayloadSize+10)
	{
		UE_LOG(LogAnalytics, Display, TEXT("Estimated Payload Size %d was significantly smaller than actual payload size %d"), PayloadSize, Payload.Len());
	}

	// reset our payload size estimate counters.
	NumEventsCached = 0;
	EventSizeEstimate = 0;
	CurrentDefaultAttributeSizeEstimate = EventCacheStatic::ComputeAttributeSize(CurrentDefaultAttributes);
	// clear the array but don't reclaim the memory.
	CachedEvents.Reset();
	// Push the current set of default attributes back onto the events list for next time we flush.
	// Can't call SetDefaultEventAttributes to do this because it already assumes we have one item in the array.
	CachedEvents.Emplace(FString(), MoveTemp(CurrentDefaultAttributes), true);

	return Payload;
}

void FAnalyticsProviderETEventCache::FlushCacheLegacy(TFunctionRef<void(const FString&, const FString&)> SendPayloadFunc)
{
	// Track the current set of default attributes. We move into this array instead of just referencing it
	// because at the end we will push the latest value back onto the list of cached events.
	// We can do this without actually copying the array this way.
	TArray<FAnalyticsEventAttribute> CurrentDefaultAttributes;

	FScopeLock ScopedLock(&CachedEventsCS);
	
	// this is a legacy pathway that doesn't accept batch payloads of cached data. We'll just send one request for each event, which will be slow for a large batch of requests at once.
	for (auto& Event : CachedEvents)
	{
		if (Event.bIsDefaultAttributes)
		{
			// This is the default attributes, so update the array.
			CurrentDefaultAttributes = MoveTemp(Event.Attributes);
		}
		else
		{
			FString EventParams;
			int PayloadNdx = 0;
			// default attributes for this event
			for (int DefaultAttributeNdx = 0; DefaultAttributeNdx < CurrentDefaultAttributes.Num() && PayloadNdx < 40; ++DefaultAttributeNdx, ++PayloadNdx)
			{
				EventParams += FString::Printf(TEXT("&AttributeName%d=%s&AttributeValue%d=%s"),
					PayloadNdx,
					*FPlatformHttp::UrlEncode(CurrentDefaultAttributes[DefaultAttributeNdx].GetName()),
					PayloadNdx,
					*FPlatformHttp::UrlEncode(CurrentDefaultAttributes[DefaultAttributeNdx].GetValue()));
			}
			// optional attributes for this event
			for (int AttrNdx = 0; AttrNdx < Event.Attributes.Num() && PayloadNdx < 40; ++AttrNdx, ++PayloadNdx)
			{
				EventParams += FString::Printf(TEXT("&AttributeName%d=%s&AttributeValue%d=%s"),
					PayloadNdx,
					*FPlatformHttp::UrlEncode(Event.Attributes[AttrNdx].GetName()),
					PayloadNdx,
					*FPlatformHttp::UrlEncode(Event.Attributes[AttrNdx].GetValue()));
			}

			SendPayloadFunc(Event.EventName, EventParams);
		}
	}
}

bool FAnalyticsProviderETEventCache::CanFlush() const
{
	return NumEventsCached > 0;
}

int FAnalyticsProviderETEventCache::GetNumCachedEvents() const
{
	return NumEventsCached;
}

int FAnalyticsProviderETEventCache::ComputeApproximateEventChars(const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attributes) const
{
	return EventCacheStatic::ComputeEventSize(EventName, Attributes, CurrentDefaultAttributeSizeEstimate);
}

int FAnalyticsProviderETEventCache::ComputeApproximatePayloadChars() const
{
	return EventCacheStatic::ComputePayloadSize(NumEventsCached, EventSizeEstimate);
}
