// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnalyticsEventAttribute.h"
#include "AnalyticsET.h"
#include "Interfaces/IAnalyticsProvider.h"

/** ET specific analytics provider instance. Exposes additional APIs to support Json-based events, using move-semantics, and allowing events to be disabled (generally via hotfixing). */
class IAnalyticsProviderET : public IAnalyticsProvider
{
public:
	////////////////////////////////////////////////////////////////////////////////////
	// Start IAnalyticsProvider overrides for deprecation.
	bool StartSession()
	{
		return StartSession(TArray<FAnalyticsEventAttribute>());
	}

	//UE_DEPRECATED(4.25, "This version of StartSession has been deprecated")
	bool StartSession(const FAnalyticsEventAttribute& Attribute)
	{
		return StartSession(TArray<FAnalyticsEventAttribute> {Attribute});
	}

	//UE_DEPRECATED(4.25, "This version of StartSession has been deprecated")
	bool StartSession(const FString& ParamName, const FString& ParamValue)
	{
		return StartSession(TArray<FAnalyticsEventAttribute> {FAnalyticsEventAttribute(ParamName, ParamValue)});
	}

	//UE_DEPRECATED(4.25, "This version of StartSession has been deprecated")
	virtual bool StartSession(const TArray<FAnalyticsEventAttribute>& Attributes) override
	{
		return StartSession(CopyTemp(Attributes));
	}

	void RecordEvent(const FString& EventName)
	{
		RecordEvent(EventName, TArray<FAnalyticsEventAttribute>());
	}

	void RecordEvent(FString&& EventName)
	{
		RecordEvent(MoveTemp(EventName), TArray<FAnalyticsEventAttribute>());
	}

	//UE_DEPRECATED(4.25, "This version of RecordEvent has been deprecated")
	void RecordEvent(const FString& EventName, const FAnalyticsEventAttribute& Attribute)
	{
		RecordEvent(EventName, TArray<FAnalyticsEventAttribute> {Attribute});
	}

	//UE_DEPRECATED(4.25, "This version of RecordEvent has been deprecated")
	void RecordEvent(const FString& EventName, const FString& ParamName, const FString& ParamValue)
	{
		RecordEvent(EventName, TArray<FAnalyticsEventAttribute> {FAnalyticsEventAttribute(ParamName, ParamValue)});
	}

	//UE_DEPRECATED(4.25, "This version of RecordEvent has been deprecated")
	virtual void RecordEvent(const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attributes) override
	{
		RecordEvent(EventName, CopyTemp(Attributes));
	}

	// End IAnalyticsProvider overrides for deprecation.
	////////////////////////////////////////////////////////////////////////////////////

	////////////////////////////////////////////////////////////////////////////////////
	// Start IAnalyticsProviderET interface using old attribute type

	/**
	* Sends an event where each attribute value is expected to be a string-ified Json value.
	* Meaning, each attribute value can be an integer, float, bool, string,
	* arbitrarily complex Json array, or arbitrarily complex Json object.
	*
	* The main thing to remember is that if you pass a Json string as an attribute value, it is up to you to
	* quote the string, as the string you pass is expected to be able to be pasted directly into a Json value. ie:
	*
	* {
	*     "EventName": "MyStringEvent",
	*     "IntAttr": 42                 <--- You simply pass this in as "42"
	*     "StringAttr": "SomeString"    <--- You must pass SomeString as "\"SomeString\""
	* }
	*
	* @param EventName			The name of the event.
	* @param AttributesJson	array of key/value attribute pairs where each value is a Json value (pure Json strings mustbe quoted by the caller).
	*/
	UE_DEPRECATED(4.25, "RecordEventJson has been deprecated, Use RecordEvent with FJsonFragment instead")
	void RecordEventJson(FString EventName, TArray<FAnalyticsEventAttribute>&& AttributesJson)
	{
		// call deprecated functions here, so turn off warnings.
		for (FAnalyticsEventAttribute& Attr : AttributesJson)
		{
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			Attr.SwitchToJsonFragment();
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}
		RecordEvent(MoveTemp(EventName), MoveTemp(AttributesJson));
	}

	/**
	* Helper for RecordEventJson when the array is not an rvalue reference.
	*
	* @param EventName			The name of the event.
	* @param AttributesJson	array of key/value attribute pairs where each value is a Json value (pure Json strings mustbe quoted by the caller).
	*/
	UE_DEPRECATED(4.25, "RecordEventJson has been deprecated, Use RecordEvent with FJsonFragment instead")
	void RecordEventJson(FString EventName, const TArray<FAnalyticsEventAttribute>& AttributesJson)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		// make a copy of the array if it's not an rvalue reference
		RecordEventJson(MoveTemp(EventName), CopyTemp(AttributesJson));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	/**
	* returns the current set of default event attributes set on the provider.
	*
	* @param Attributes array of attributes that should be appended to every event.
	*/
	UE_DEPRECATED(4.25, "This version of GetDefaultEventAttributes has been deprecated, Use GetDefaultEventAttributesSafe instead")
	const TArray<FAnalyticsEventAttribute>& GetDefaultEventAttributes() const
	{
		// Support the old (unsafe) API by copying the values being returned. 
		UnsafeDefaultAttributes = GetDefaultEventAttributesSafe();
		return UnsafeDefaultAttributes;
	}

	// End IAnalyticsProviderET interface using old attribute type
	////////////////////////////////////////////////////////////////////////////////////

	/**
	 * Special setter to set the AppID, something that is not normally allowed for third party analytics providers.
	 *
	 * @param AppID The new AppID to set
	 */
	virtual void SetAppID(FString&& AppID) = 0;

	
	/**
	 * Method to get the AppID (APIKey)
	 *
	 * @return the AppID (APIKey)
	 */
	const FString& GetAppID() const { return GetConfig().APIKeyET; }

	/**
	 * Sets the AppVersion.
	 *
	 * @param AppVersion The new AppVersion.
	 */
	virtual void SetAppVersion(FString&& AppVersion) = 0;

	/**
	* Method to get the AppVersion
	*
	* @return the AppVersion
	*/
	const FString& GetAppVersion() const { return GetConfig().AppVersionET; }

	bool StartSession(FString InSessionID)
	{
		StartSession(MoveTemp(InSessionID), TArray<FAnalyticsEventAttribute>());
	}

	bool StartSession(TArray<FAnalyticsEventAttribute>&& Attributes)
	{
		FGuid SessionGUID;
		FPlatformMisc::CreateGuid(SessionGUID);
		return StartSession(SessionGUID.ToString(EGuidFormats::DigitsWithHyphensInBraces), MoveTemp(Attributes));
	}

	/**
	 * Primary StartSession API. Allow move semantics to capture the attributes.
	 */
	virtual bool StartSession(FString InSessionID, TArray<FAnalyticsEventAttribute>&& Attributes) = 0;

	/**
	* Allows higher level code to abort logic to set up for a RecordEvent call by checking the filter that will be used to send the event first.
	*
	* @param EventName The name of the event.
	* @return true if the event will be recorded using the currently installed ShouldRecordEvent function
	*/
	virtual bool ShouldRecordEvent(const FString& EventName) const = 0;

	/**
	 * Primary RecordEvent API. Allow move semantics to capture the attributes.
	 */
	virtual void RecordEvent(FString EventName, TArray<FAnalyticsEventAttribute>&& Attributes) = 0;


	/**
	 * Sets an array of attributes that will automatically be appended to any event that is sent.
	 * Logical effect is like adding them to all events before calling RecordEvent.
	 * Practically, it is implemented much more efficiently from a storage and allocation perspective.
	 */
	virtual void SetDefaultEventAttributes(TArray<FAnalyticsEventAttribute>&& Attributes) = 0;

	/** 
	 * @return the current array of default attributes.
	 */ 
	virtual TArray<FAnalyticsEventAttribute> GetDefaultEventAttributesSafe() const = 0;

	/**
	 * Used with GetDefaultAttribute to iterate over the default attributes.
	 * 
	 * @return the number of default attributes are currently being applied.
	 */
	virtual int32 GetDefaultEventAttributeCount() const = 0;

	/**
	 * Used with GetDefaultEventAttributeCount to iterate over the default attributes.
	 *
	 * Range checking is not done, similar to TArray. Use GetDefaultAttributeCount() first!
	 * @return one attribute of the default attributes so we don't have to copy the entire attribute array.
	 */
	virtual FAnalyticsEventAttribute GetDefaultEventAttribute(int AttributeIndex) const = 0;

	/**
	 * Updates the default URL endpoint and AltDomains.
	 */
	virtual void SetURLEndpoint(const FString& UrlEndpoint, const TArray<FString>& AltDomains) = 0;

	typedef TFunction<void(const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attrs, bool bJson)> OnEventRecorded;

	/**
	* Set a callback to be invoked any time an event is queued.
	*
	* @param the callback
	*/
	virtual void SetEventCallback(const OnEventRecorded& Callback) = 0;

	/**
	* Blocks execution in the thread until all events have been flushed to the network.
	*/
	virtual void BlockUntilFlushed(float InTimeoutSec) = 0;

	/**
	 * Return the current provider configuration.
	 */
	virtual const FAnalyticsET::Config& GetConfig() const = 0;

	/** Callback used before any event is actually sent. Allows higher level code to disable events. */
	typedef TFunction<bool(const IAnalyticsProviderET& ThisProvider, const FString& EventName)> ShouldRecordEventFunction;

	/** Set an event filter to dynamically control whether an event should be sent. */
	virtual void SetShouldRecordEventFunc(const ShouldRecordEventFunction& ShouldRecordEventFunc) = 0;

private:
	/** Needed to support the old, unsafe GetDefaultAttributes() API. */
	mutable TArray<FAnalyticsEventAttribute> UnsafeDefaultAttributes;
};
