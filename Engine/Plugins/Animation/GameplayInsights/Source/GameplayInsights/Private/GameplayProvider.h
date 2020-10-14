// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IGameplayProvider.h"
#include "Model/PointTimeline.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"
#include "Containers/StringView.h"
#include "Model/IntervalTimeline.h"

namespace Trace { class IAnalysisSession; }

class FGameplayProvider : public IGameplayProvider
{
public:
	static FName ProviderName;

	FGameplayProvider(Trace::IAnalysisSession& InSession);

	/** IGameplayProvider interface */
	virtual bool ReadObjectEventsTimeline(uint64 InObjectId, TFunctionRef<void(const ObjectEventsTimeline&)> Callback) const override;
	virtual bool ReadObjectEvent(uint64 InObjectId, uint64 InMessageId, TFunctionRef<void(const FObjectEventMessage&)> Callback) const override;
	virtual bool ReadObjectPropertiesTimeline(uint64 InObjectId, TFunctionRef<void(const ObjectPropertiesTimeline&)> Callback) const override;
	virtual void EnumerateObjectPropertyValues(uint64 InObjectId, const FObjectPropertiesMessage& InMessage, TFunctionRef<void(const FObjectPropertyValue&)> Callback) const override;
	virtual void EnumerateObjects(TFunctionRef<void(const FObjectInfo&)> Callback) const override;
	virtual const FClassInfo* FindClassInfo(uint64 InClassId) const override;
	virtual const FClassInfo* FindClassInfo(const TCHAR* InClassPath) const override;
	virtual const FObjectInfo* FindObjectInfo(uint64 InObjectId) const override;
	virtual const FWorldInfo* FindWorldInfo(uint64 InObjectId) const override;
	virtual const FWorldInfo* FindWorldInfoFromObject(uint64 InObjectId) const override;
	virtual bool IsWorld(uint64 InObjectId) const override;
	virtual const FClassInfo& GetClassInfo(uint64 InClassId) const override;
	virtual const FClassInfo& GetClassInfoFromObject(uint64 InObjectId) const override;
	virtual const FObjectInfo& GetObjectInfo(uint64 InObjectId) const override;
	virtual FOnObjectEndPlay& OnObjectEndPlay() override { return OnObjectEndPlayDelegate; }
	virtual const TCHAR* GetPropertyName(uint32 InPropertyStringId) const override;

	/** Add a class message */
	void AppendClass(uint64 InClassId, uint64 InSuperId, const TCHAR* InClassName, const TCHAR* InClassPathName);

	/** Add an object message */
	void AppendObject(uint64 InObjectId, uint64 InOuterId, uint64 InClassId, const TCHAR* InObjectName, const TCHAR* InObjectPathName);

	/** Add an object event message */
	void AppendObjectEvent(uint64 InObjectId, double InTime, const TCHAR* InEvent);

	/** Add a world message */
	void AppendWorld(uint64 InObjectId, int32 InPIEInstanceId, uint8 InType, uint8 InNetMode, bool bInIsSimulating);

	/** Add a class property string ID message */
	void AppendClassPropertyStringId(uint32 InStringId, const FStringView& InString);

	/** Add a class property message */
	void AppendClassProperty(uint64 InClassId, int32 InId, int32 InParentId, uint32 InTypeStringId, uint32 InKeyStringId);

	/** Add a properties start message */
	void AppendPropertiesStart(uint64 InObjectId, double InTime, uint64 InEventId);

	/** Add a properties end message */
	void AppendPropertiesEnd(uint64 InObjectId, double InTime);

	/** Add a property value message */
	void AppendPropertyValue(uint64 InObjectId, double InTime, uint64 InEventId, int32 InPropertyId, const FStringView& InValue);

	/** Check whether we have any data */
	bool HasAnyData() const;

	/** Check whether we have any object property data */
	bool HasObjectProperties() const;

private:
	Trace::IAnalysisSession& Session;

	/** All class info, grow only for stable indices */
	TArray<FClassInfo> ClassInfos;

	/** All object info, grow only for stable indices */
	TArray<FObjectInfo> ObjectInfos;

	/** All world info, grow only for stable indices */
	TArray<FWorldInfo> WorldInfos;

	/** Classes that are in use. Map from Id to ClassInfo index */
	TMap<uint64, int32> ClassIdToIndexMap;

	/** Objects that are in use. Map from Id to ObjectInfo index */
	TMap<uint64, int32> ObjectIdToIndexMap;

	/** Worlds that are in use. Map from Id to WorldInfo index */
	TMap<uint64, int32> WorldIdToIndexMap;

	/** Map from object Ids to timeline index */
	TMap<uint64, uint32> ObjectIdToEventTimelines;
	TMap<uint64, uint32> ObjectIdToPropertiesStorage;

	struct FObjectPropertiesStorage
	{
		double OpenStartTime;
		uint64 OpenEventId;
		FObjectPropertiesMessage OpenEvent;
		TSharedPtr<Trace::TIntervalTimeline<FObjectPropertiesMessage>> Timeline;
		TArray<FObjectPropertyValue> Values;
	};

	/** Message storage */
	TArray<TSharedRef<Trace::TPointTimeline<FObjectEventMessage>>> EventTimelines;
	TArray<TSharedRef<FObjectPropertiesStorage>> PropertiesStorage;

	/** Map of class path name to ClassInfo index */
	TMap<FStringView, int32> ClassPathNameToIndexMap;

	/** EndPlay event text */
	const TCHAR* EndPlayEvent;

	/** Delegate fired when an object receives an end play event */
	FOnObjectEndPlay OnObjectEndPlayDelegate;

	/** Map from string ID to stored string */
	TMap<uint32, const TCHAR*> PropertyStrings;

	/** Whether we have any data */
	bool bHasAnyData;

	/** Whether we have any object properties */
	bool bHasObjectProperties;
};