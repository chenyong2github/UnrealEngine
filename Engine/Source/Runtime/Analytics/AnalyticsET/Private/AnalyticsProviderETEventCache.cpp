// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnalyticsProviderETEventCache.h"
#include "IAnalyticsProviderET.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "PlatformHttp.h"

FAnalyticsProviderETEventCache::FAnalyticsProviderETEventCache()
{
	// if we are caching events, presize the array to max size. Otherwise, we will never have more than two entries in the array (one for the default attributes, one for the actual event)
	CachedEvents.Reserve(2);
	// make sure that we always start with one control event in the CachedEvents array.
	CachedEvents.Emplace(FString(), TArray<FAnalyticsEventAttribute>(), false, true);
}

void FAnalyticsProviderETEventCache::AddToCache(FString EventName, TArray<FAnalyticsEventAttribute>&& Attributes, bool bIsJsonEvent)
{
	FScopeLock ScopedLock(&CachedEventsCS);
	CachedEvents.Emplace(MoveTemp(EventName), MoveTemp(Attributes), bIsJsonEvent, false);
}

void FAnalyticsProviderETEventCache::SetDefaultAttributes(TArray<FAnalyticsEventAttribute>&& DefaultAttributes)
{
	FScopeLock ScopedLock(&CachedEventsCS);
	// we know we always have one entry in CachedEvents, so no need to check for Num() > 0.
	// If we are trying to add two default attribute events in a row, just overwrite the last one.
	if (CachedEvents.Last().bIsDefaultAttributes)
	{
		CachedEvents.Last() = FAnalyticsEventEntry(FString(), MoveTemp(DefaultAttributes), false, true);
	}
	else
	{
		CachedEvents.Emplace(FString(), MoveTemp(DefaultAttributes), false, true);
	}
}

TArray<FAnalyticsEventAttribute> FAnalyticsProviderETEventCache::GetDefaultAttributes() const
{
	FScopeLock ScopedLock(&CachedEventsCS);

	int32 DefaultIndex = CachedEvents.FindLastByPredicate([](const FAnalyticsEventEntry& Entry) { return Entry.bIsDefaultAttributes == 1; });
	checkf(DefaultIndex != INDEX_NONE, TEXT("failed to find default attributes entry in analytics cached events list"));
	return CachedEvents[DefaultIndex].Attributes;
}

FString FAnalyticsProviderETEventCache::FlushCache(SIZE_T* OutEventCount)
{
	FDateTime CurrentTime = FDateTime::UtcNow();

	// Track the current set of default attributes. We move into this array instead of just referencing it
	// because at the end we will push the latest value back onto the list of cached events.
	// We can do this without actually copying the array this way.
	TArray<FAnalyticsEventAttribute> CurrentDefaultAttributes;

	FString Payload;
	TSharedRef< TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR> > > JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR> >::Create(&Payload);
	JsonWriter->WriteObjectStart();
	JsonWriter->WriteArrayStart(TEXT("Events"));

	FScopeLock ScopedLock(&CachedEventsCS);
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
			JsonWriter->WriteObjectStart();
			JsonWriter->WriteValue(TEXT("EventName"), Entry.EventName);
			FString DateOffset = (CurrentTime - Entry.TimeStamp).ToString();
			JsonWriter->WriteValue(TEXT("DateOffset"), DateOffset);
			// default attributes for this event
			for (const FAnalyticsEventAttribute& Attr : CurrentDefaultAttributes)
			{
				switch (Attr.AttrType)
				{
				case FAnalyticsEventAttribute::AttrTypeEnum::String:
					JsonWriter->WriteValue(Attr.AttrName, Attr.AttrValueString);
					break;
				case FAnalyticsEventAttribute::AttrTypeEnum::Number:
					JsonWriter->WriteValue(Attr.AttrName, Attr.ToString());
					break;
				case FAnalyticsEventAttribute::AttrTypeEnum::Boolean:
					JsonWriter->WriteValue(Attr.AttrName, Attr.ToString());
					break;
				case FAnalyticsEventAttribute::AttrTypeEnum::JsonFragment:
					JsonWriter->WriteRawJSONValue(Attr.AttrName, Attr.AttrValueString);
					break;
				}
			}
			// optional attributes for this event
			if (!Entry.bIsJsonEvent)
			{
				for (const FAnalyticsEventAttribute& Attr : Entry.Attributes)
				{
					switch (Attr.AttrType)
					{
					case FAnalyticsEventAttribute::AttrTypeEnum::String:
						JsonWriter->WriteValue(Attr.AttrName, Attr.AttrValueString);
						break;
					case FAnalyticsEventAttribute::AttrTypeEnum::Number:
						JsonWriter->WriteValue(Attr.AttrName, Attr.ToString());
						break;
					case FAnalyticsEventAttribute::AttrTypeEnum::Boolean:
						JsonWriter->WriteValue(Attr.AttrName, Attr.ToString());
						break;
					case FAnalyticsEventAttribute::AttrTypeEnum::JsonFragment:
						JsonWriter->WriteRawJSONValue(Attr.AttrName, Attr.AttrValueString);
						break;
					}
				}
			}
			else
			{
				for (const FAnalyticsEventAttribute& Attr : Entry.Attributes)
				{
					JsonWriter->WriteRawJSONValue(Attr.AttrName, Attr.AttrValueString);
				}
			}
			JsonWriter->WriteObjectEnd();
		}
	}

	JsonWriter->WriteArrayEnd();
	JsonWriter->WriteObjectEnd();
	JsonWriter->Close();

	// clear the array but don't reclaim the memory.
	CachedEvents.Reset();
	// Push the current set of default attributes back onto the events list for next time we flush.
	// Can't call SetDefaultEventAttributes to do this because it already assumes we have one item in the array.
	CachedEvents.Emplace(FString(), MoveTemp(CurrentDefaultAttributes), false, true);

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
					*FPlatformHttp::UrlEncode(CurrentDefaultAttributes[DefaultAttributeNdx].AttrName),
					PayloadNdx,
					*FPlatformHttp::UrlEncode(CurrentDefaultAttributes[DefaultAttributeNdx].ToString()));
			}
			// optional attributes for this event
			for (int AttrNdx = 0; AttrNdx < Event.Attributes.Num() && PayloadNdx < 40; ++AttrNdx, ++PayloadNdx)
			{
				EventParams += FString::Printf(TEXT("&AttributeName%d=%s&AttributeValue%d=%s"),
					PayloadNdx,
					*FPlatformHttp::UrlEncode(Event.Attributes[AttrNdx].AttrName),
					PayloadNdx,
					*FPlatformHttp::UrlEncode(Event.Attributes[AttrNdx].ToString()));
			}

			SendPayloadFunc(Event.EventName, EventParams);
		}
	}
}

bool FAnalyticsProviderETEventCache::CanFlush() const
{
	FScopeLock ScopedLock(&CachedEventsCS);
	return CachedEvents.Num() > 1;
}

int FAnalyticsProviderETEventCache::GetNumCachedEvents() const
{
	FScopeLock ScopedLock(&CachedEventsCS);
	return CachedEvents.Num();
}
