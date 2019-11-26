// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Containers/Timelines.h"

struct FClassInfo
{	
	uint64 Id = 0;
	uint64 SuperId = 0;
	const TCHAR* Name = nullptr;
	const TCHAR* PathName = nullptr;
};

struct FObjectInfo
{	
	uint64 Id = 0;
	uint64 OuterId = 0;
	uint64 ClassId = 0;
	const TCHAR* Name = nullptr;
	const TCHAR* PathName = nullptr;
};

struct FObjectEventMessage
{
	uint64 Id = 0;
	const TCHAR* Name = nullptr;
};

// Delegate fired when an object receives an end play event
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnObjectEndPlay, uint64 /*ObjectId*/, double /*Time*/, const FObjectInfo& /*ObjectInfo*/);

class IGameplayProvider : public Trace::IProvider
{
public:
	typedef Trace::ITimeline<FObjectEventMessage> ObjectEventsTimeline;

	virtual bool ReadObjectEventsTimeline(uint64 InObjectId, TFunctionRef<void(const ObjectEventsTimeline&)> Callback) const = 0;
	virtual bool ReadObjectEvent(uint64 InObjectId, uint64 InMessageId, TFunctionRef<void(const FObjectEventMessage&)> Callback) const = 0;
	virtual void EnumerateObjects(TFunctionRef<void(const FObjectInfo&)> Callback) const = 0;
	virtual const FClassInfo* FindClassInfo(uint64 InClassId) const = 0;
	virtual const FObjectInfo* FindObjectInfo(uint64 InObjectId) const = 0;
	virtual FOnObjectEndPlay& OnObjectEndPlay() = 0;
};