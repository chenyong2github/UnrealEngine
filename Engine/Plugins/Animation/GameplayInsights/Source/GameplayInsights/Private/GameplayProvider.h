// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IGameplayProvider.h"
#include "Model/PointTimeline.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"

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
	virtual const FObjectInfo* FindObjectInfo(uint64 InObjectId) const override;
	virtual FOnObjectEndPlay& OnObjectEndPlay() override { return OnObjectEndPlayDelegate; }

	/** Add a class message */
	void AppendClass(uint64 InClassId, uint64 InSuperId, const TCHAR* InClassName, const TCHAR* InClassPathName);

	/** Add an object message */
	void AppendObject(uint64 InObjectId, uint64 InOuterId, uint64 InClassId, const TCHAR* InObjectName, const TCHAR* InObjectPathName);

	/** Add an object event message */
	void AppendObjectEvent(uint64 InObjectId, double InTime, const TCHAR* InEvent);

private:
	Trace::IAnalysisSession& Session;

	/** All class info, grow only for stable indices */
	TArray<FClassInfo> ClassInfos;

	/** All object info, grow only for stable indices */
	TArray<FObjectInfo> ObjectInfos;

	/** Classes that are in use. Map from Id to ClassInfo index */
	TMap<uint64, int32> ClassIdToIndexMap;

	/** Objects that are in use. Map from Id to ObjectInfo index */
	TMap<uint64, int32> ObjectIdToIndexMap;

	/** Map from object Id to timeline index */
	TMap<uint64, uint32> ObjectIdToEventTimelines;

	/** Message storage */
	TArray<TSharedRef<Trace::TPointTimeline<FObjectEventMessage>>> EventTimelines;

	/** EndPlay event text */
	const TCHAR* EndPlayEvent;

	/** Delegate fired when an object receives an end play event */
	FOnObjectEndPlay OnObjectEndPlayDelegate;
};