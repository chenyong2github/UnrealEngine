// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IGameplayProvider.h"
#include "Model/PointTimeline.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"
#include "Containers/StringView.h"

namespace Trace { class IAnalysisSession; }

class FGameplayProvider : public IGameplayProvider
{
public:
	static FName ProviderName;

	FGameplayProvider(Trace::IAnalysisSession& InSession);

	/** IGameplayProvider interface */
	virtual bool ReadObjectEventsTimeline(uint64 InObjectId, TFunctionRef<void(const ObjectEventsTimeline&)> Callback) const override;
	virtual bool ReadObjectEvent(uint64 InObjectId, uint64 InMessageId, TFunctionRef<void(const FObjectEventMessage&)> Callback) const override;
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

	/** Add a class message */
	void AppendClass(uint64 InClassId, uint64 InSuperId, const TCHAR* InClassName, const TCHAR* InClassPathName);

	/** Add an object message */
	void AppendObject(uint64 InObjectId, uint64 InOuterId, uint64 InClassId, const TCHAR* InObjectName, const TCHAR* InObjectPathName);

	/** Add an object event message */
	void AppendObjectEvent(uint64 InObjectId, double InTime, const TCHAR* InEvent);

	/** Add a world message */
	void AppendWorld(uint64 InObjectId, int32 InPIEInstanceId, uint8 InType, uint8 InNetMode, bool bInIsSimulating);

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

	/** Map from object Id to timeline index */
	TMap<uint64, uint32> ObjectIdToEventTimelines;

	/** Message storage */
	TArray<TSharedRef<Trace::TPointTimeline<FObjectEventMessage>>> EventTimelines;

	/** Map of class path name to ClassInfo index */
	TMap<FStringView, int32> ClassPathNameToIndexMap;

	/** EndPlay event text */
	const TCHAR* EndPlayEvent;

	/** Delegate fired when an object receives an end play event */
	FOnObjectEndPlay OnObjectEndPlayDelegate;
};