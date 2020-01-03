// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayProvider.h"

FName FGameplayProvider::ProviderName("GameplayProvider");

#define LOCTEXT_NAMESPACE "GameplayProvider"

FGameplayProvider::FGameplayProvider(Trace::IAnalysisSession& InSession)
	: Session(InSession)
	, EndPlayEvent(nullptr)
{
}

bool FGameplayProvider::ReadObjectEventsTimeline(uint64 InObjectId, TFunctionRef<void(const ObjectEventsTimeline&)> Callback) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = ObjectIdToEventTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(EventTimelines.Num()))
		{
			Callback(*EventTimelines[*IndexPtr]);
			return true;
		}
	}

	return false;
}

bool FGameplayProvider::ReadObjectEvent(uint64 InObjectId, uint64 InMessageId, TFunctionRef<void(const FObjectEventMessage&)> Callback) const
{
	Session.ReadAccessCheck();

	return ReadObjectEventsTimeline(InObjectId, [&Callback, &InMessageId](const ObjectEventsTimeline& InTimeline)
	{
		if(InMessageId < InTimeline.GetEventCount())
		{
			Callback(InTimeline.GetEvent(InMessageId));
		}
	});
}

void FGameplayProvider::EnumerateObjects(TFunctionRef<void(const FObjectInfo&)> Callback) const
{
	Session.ReadAccessCheck();

	for(const FObjectInfo& ObjectInfo : ObjectInfos)
	{
		Callback(ObjectInfo);
	}
}

const FClassInfo* FGameplayProvider::FindClassInfo(uint64 InClassId) const
{
	Session.ReadAccessCheck();

	const int32* ClassIndex = ClassIdToIndexMap.Find(InClassId);
	if(ClassIndex != nullptr)
	{
		return &ClassInfos[*ClassIndex];
	}

	return nullptr;
}

const FObjectInfo* FGameplayProvider::FindObjectInfo(uint64 InObjectId) const
{
	Session.ReadAccessCheck();

	const int32* ObjectIndex = ObjectIdToIndexMap.Find(InObjectId);
	if(ObjectIndex != nullptr)
	{
		return &ObjectInfos[*ObjectIndex];
	}

	return nullptr;
}

const FClassInfo& FGameplayProvider::GetClassInfo(uint64 InClassId) const
{
	const FClassInfo* ClassInfo = FindClassInfo(InClassId);
	if(ClassInfo)
	{
		return *ClassInfo;
	}

	static FText UnknownText(LOCTEXT("Unknown", "Unknown"));
	static FClassInfo DefaultClassInfo = { 0, 0, *UnknownText.ToString(), *UnknownText.ToString() };
	return DefaultClassInfo;
}

const FClassInfo& FGameplayProvider::GetClassInfoFromObject(uint64 InObjectId) const
{
	const FObjectInfo* ObjectInfo = FindObjectInfo(InObjectId);
	if(ObjectInfo)
	{
		const FClassInfo* ClassInfo = FindClassInfo(ObjectInfo->ClassId);
		if(ClassInfo)
		{
			return *ClassInfo;
		}
	}

	static FText UnknownText(LOCTEXT("Unknown", "Unknown"));
	static FClassInfo DefaultClassInfo = { 0, 0, *UnknownText.ToString(), *UnknownText.ToString() };
	return DefaultClassInfo;
}

const FObjectInfo& FGameplayProvider::GetObjectInfo(uint64 InObjectId) const
{
	const FObjectInfo* ObjectInfo = FindObjectInfo(InObjectId);
	if(ObjectInfo)
	{
		return *ObjectInfo;
	}

	static FText UnknownText(LOCTEXT("Unknown", "Unknown"));
	static FObjectInfo DefaultObjectInfo = { 0, 0, 0, *UnknownText.ToString(), *UnknownText.ToString() };
	return DefaultObjectInfo;
}

void FGameplayProvider::AppendClass(uint64 InClassId, uint64 InSuperId, const TCHAR* InClassName, const TCHAR* InClassPathName)
{
	Session.WriteAccessCheck();

	if(ClassIdToIndexMap.Find(InClassId) == nullptr)
	{
		const TCHAR* NewClassName = Session.StoreString(InClassName);
		const TCHAR* NewClassPathName = Session.StoreString(InClassPathName);

		FClassInfo NewClassInfo;
		NewClassInfo.Id = InClassId;
		NewClassInfo.SuperId = InSuperId;
		NewClassInfo.Name = NewClassName;
		NewClassInfo.PathName = NewClassPathName;

		int32 NewClassInfoIndex = ClassInfos.Add(NewClassInfo);
		ClassIdToIndexMap.Add(InClassId, NewClassInfoIndex);
	}
}

void FGameplayProvider::AppendObject(uint64 InObjectId, uint64 InOuterId, uint64 InClassId, const TCHAR* InObjectName, const TCHAR* InObjectPathName)
{
	Session.WriteAccessCheck();

	if(ObjectIdToIndexMap.Find(InObjectId) == nullptr)
	{
		const TCHAR* NewObjectName = Session.StoreString(InObjectName);
		const TCHAR* NewObjectPathName = Session.StoreString(InObjectPathName);

		FObjectInfo NewObjectInfo;
		NewObjectInfo.Id = InObjectId;
		NewObjectInfo.OuterId = InOuterId;
		NewObjectInfo.ClassId = InClassId;
		NewObjectInfo.Name = NewObjectName;
		NewObjectInfo.PathName = NewObjectPathName;

		int32 NewObjectInfoIndex = ObjectInfos.Add(NewObjectInfo);
		ObjectIdToIndexMap.Add(InObjectId, NewObjectInfoIndex);
	}
}

void FGameplayProvider::AppendObjectEvent(uint64 InObjectId, double InTime, const TCHAR* InEventName)
{
	Session.WriteAccessCheck();

	// Important events need some extra routing
	if(EndPlayEvent == nullptr)
	{
		EndPlayEvent = Session.StoreString(TEXT("EndPlay"));
	}

	TSharedPtr<Trace::TPointTimeline<FObjectEventMessage>> Timeline;
	uint32* IndexPtr = ObjectIdToEventTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		Timeline = EventTimelines[*IndexPtr];
	}
	else
	{
		Timeline = MakeShared<Trace::TPointTimeline<FObjectEventMessage>>(Session.GetLinearAllocator());
		ObjectIdToEventTimelines.Add(InObjectId, EventTimelines.Num());
		EventTimelines.Add(Timeline.ToSharedRef());
	}

	FObjectEventMessage Message;
	Message.Id = InObjectId;
	Message.Name = Session.StoreString(InEventName);

	if(Message.Name == EndPlayEvent)
	{
		if(int32* ObjectInfoIndex = ObjectIdToIndexMap.Find(InObjectId))
		{
			OnObjectEndPlayDelegate.Broadcast(InObjectId, InTime, ObjectInfos[*ObjectInfoIndex]);
		}
	}

	Timeline->AppendEvent(InTime, Message);

	Session.UpdateDurationSeconds(InTime);
}

#undef LOCTEXT_NAMESPACE