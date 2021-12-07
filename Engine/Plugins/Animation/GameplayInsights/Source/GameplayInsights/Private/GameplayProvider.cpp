// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayProvider.h"

#include "Model/AsyncEnumerateTask.h"

FName FGameplayProvider::ProviderName("GameplayProvider");

#define LOCTEXT_NAMESPACE "GameplayProvider"

FGameplayProvider::FGameplayProvider(TraceServices::IAnalysisSession& InSession)
	: Session(InSession)
	, EndPlayEvent(nullptr)
	, PawnPossession(InSession.GetLinearAllocator())
	, ObjectLifetimes(InSession.GetLinearAllocator())
	, bHasAnyData(false)
	, bHasObjectProperties(false)
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

bool FGameplayProvider::ReadObjectPropertiesTimeline(uint64 InObjectId, TFunctionRef<void(const ObjectPropertiesTimeline&)> Callback) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = ObjectIdToPropertiesStorage.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(ObjectIdToPropertiesStorage.Num()))
		{
			Callback(*PropertiesStorage[*IndexPtr]->Timeline);
			return true;
		}
	}

	return false;
}

void FGameplayProvider::EnumerateObjectPropertyValues(uint64 InObjectId, const FObjectPropertiesMessage& InMessage, TFunctionRef<void(const FObjectPropertyValue&)> Callback) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = ObjectIdToPropertiesStorage.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(ObjectIdToPropertiesStorage.Num()))
		{
			TSharedRef<FObjectPropertiesStorage> Storage = PropertiesStorage[*IndexPtr];
			for(int64 ValueIndex = InMessage.PropertyValueStartIndex; ValueIndex < InMessage.PropertyValueEndIndex; ++ValueIndex)
			{
				Callback(Storage->Values[ValueIndex]);
			}
		}
	}
}

void FGameplayProvider::EnumerateObjects(TFunctionRef<void(const FObjectInfo&)> Callback) const
{
	Session.ReadAccessCheck();

	for(const FObjectInfo& ObjectInfo : ObjectInfos)
	{
		Callback(ObjectInfo);
	}
}

void FGameplayProvider::EnumerateObjects(double StartTime, double EndTime, TFunctionRef<void(const FObjectInfo&)> Callback) const
{
	Session.ReadAccessCheck();

	ObjectLifetimes.EnumerateEvents(StartTime, EndTime,
		[this, Callback](double InStartTime, double InEndTime, uint32 InDepth, const FObjectExistsMessage& ExistsMessage)
		{
			checkSlow(ObjectIdToIndexMap.Contains(ExistsMessage.ObjectId));
			Callback(ObjectInfos[ObjectIdToIndexMap[ExistsMessage.ObjectId]]);
			return TraceServices::EEventEnumerate::Continue;
		});
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

const FClassInfo* FGameplayProvider::FindClassInfo(const TCHAR* InClassPath) const
{
	Session.ReadAccessCheck();

	const int32* ClassIndex = ClassPathNameToIndexMap.Find(InClassPath);
	if (ClassIndex != nullptr)
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

const FWorldInfo* FGameplayProvider::FindWorldInfo(uint64 InObjectId) const
{
	Session.ReadAccessCheck();

	const int32* WorldIndex = WorldIdToIndexMap.Find(InObjectId);
	if (WorldIndex != nullptr)
	{
		return &WorldInfos[*WorldIndex];
	}

	return nullptr;
}

const FWorldInfo* FGameplayProvider::FindWorldInfoFromObject(uint64 InObjectId) const
{
	const FClassInfo* WorldClass = FindClassInfo(TEXT("/Script/Engine.World"));
	if(WorldClass)
	{
		// Traverse outer chain until we find a world
		const FObjectInfo* ObjectInfo = FindObjectInfo(InObjectId);
		while (ObjectInfo != nullptr)
		{
			if (ObjectInfo->ClassId == WorldClass->Id)
			{
				return FindWorldInfo(ObjectInfo->Id);
			}

			ObjectInfo = FindObjectInfo(ObjectInfo->OuterId);
		}
	}

	return nullptr;
}

bool FGameplayProvider::IsWorld(uint64 InObjectId) const
{
	const FClassInfo* WorldClass = FindClassInfo(TEXT("/Script/Engine.World"));
	if (WorldClass)
	{
		const FObjectInfo* ObjectInfo = FindObjectInfo(InObjectId);
		return ObjectInfo->ClassId == WorldClass->Id;
	}

	return false;
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

const TCHAR* FGameplayProvider::GetPropertyName(uint32 InPropertyStringId) const
{
	if(const TCHAR*const* FoundString = PropertyStrings.Find(InPropertyStringId))
	{
		return *FoundString;
	}

	static FText UnknownText(LOCTEXT("Unknown", "Unknown"));
	return *UnknownText.ToString();
}

void FGameplayProvider::AppendClass(uint64 InClassId, uint64 InSuperId, const TCHAR* InClassName, const TCHAR* InClassPathName)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;

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
		ClassPathNameToIndexMap.Add(NewClassPathName, NewClassInfoIndex);
	}
}

void FGameplayProvider::AppendObject(uint64 InObjectId, uint64 InOuterId, uint64 InClassId, const TCHAR* InObjectName, const TCHAR* InObjectPathName)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;

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

void FGameplayProvider::AppendObjectLifetimeBegin(uint64 InObjectId, double InTime)
{
	Session.WriteAccessCheck();
	bHasAnyData = true;

	if (InObjectId)
	{
		FObjectExistsMessage Message;
		Message.ObjectId = InObjectId;
		ActiveObjectLifetimes.Add(InObjectId, ObjectLifetimes.AppendBeginEvent(InTime, Message));
	}
}

void FGameplayProvider::AppendObjectLifetimeEnd(uint64 InObjectId, double InTime)
{
	Session.WriteAccessCheck();
	bHasAnyData = true;

	if (const uint64 *FoundIndex = ActiveObjectLifetimes.Find(InObjectId))
	{
		ObjectLifetimes.EndEvent(*FoundIndex, InTime);
		ActiveObjectLifetimes.Remove(InObjectId);
	}

	if(int32* ObjectInfoIndex = ObjectIdToIndexMap.Find(InObjectId))
	{
		OnObjectEndPlayDelegate.Broadcast(InObjectId, InTime, ObjectInfos[*ObjectInfoIndex]);
	}
}

void FGameplayProvider::AppendObjectEvent(uint64 InObjectId, double InTime, const TCHAR* InEventName)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;

	// Important events need some extra routing
	if(EndPlayEvent == nullptr)
	{
		EndPlayEvent = Session.StoreString(TEXT("EndPlay"));
	}

	TSharedPtr<TraceServices::TPointTimeline<FObjectEventMessage>> Timeline;
	uint32* IndexPtr = ObjectIdToEventTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		Timeline = EventTimelines[*IndexPtr];
	}
	else
	{
		Timeline = MakeShared<TraceServices::TPointTimeline<FObjectEventMessage>>(Session.GetLinearAllocator());
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

void FGameplayProvider::AppendPawnPossess(uint64 InControllerId, uint64 InPawnId, double InTime)
{
	Session.WriteAccessCheck();
	bHasAnyData = true;

	// End any active controller attachment interval for this controller
	if (const uint64 *FoundIndex = ActivePawnPossession.Find(InControllerId))
	{
		PawnPossession.EndEvent(*FoundIndex, InTime);
		ActivePawnPossession.Remove(InControllerId);
	}
	
	if (InPawnId)
	{
		FPawnPossessMessage Message;
		Message.ControllerId = InControllerId;
		Message.PawnId = InPawnId;
		ActivePawnPossession.Add(InControllerId, PawnPossession.AppendBeginEvent(InTime, Message));
	}
}

uint64 FGameplayProvider::FindPossessingController(uint64 PawnId, double Time) const
{
	uint64 ControllerId = 0;
	PawnPossession.EnumerateEvents(Time,Time,[&ControllerId, PawnId](double StartTime,double EndTime, const FPawnPossessMessage Message)
	{
		if (Message.PawnId == PawnId)
		{
			ControllerId = Message.ControllerId;
			return TraceServices::EEventEnumerate::Stop;
		}
		return TraceServices::EEventEnumerate::Continue;
	});
	return ControllerId;
}

void FGameplayProvider::ReadViewTimeline(TFunctionRef<void(const IGameplayProvider::ViewTimeline&)> Callback) const
{
	Session.ReadAccessCheck();

	if (ViewTimeline.IsValid())
	{
		Callback(*ViewTimeline);
	}
}

void FGameplayProvider::AppendView(uint64 InPlayerId, double InTime, const FVector& InPosition, const FRotator& InRotation, float InFov, float InAspectRatio)
{
	Session.WriteAccessCheck();

	if (!ViewTimeline.IsValid())
	{
		ViewTimeline = MakeShared<TraceServices::TPointTimeline<FViewMessage>>(Session.GetLinearAllocator());
	}

	bHasAnyData = true;

	FViewMessage Message;
	Message.PlayerId = InPlayerId;
	Message.Position = InPosition;
	Message.Rotation = InRotation;
	Message.Fov = InFov;
	Message.AspectRatio = InAspectRatio;

	ViewTimeline->AppendEvent(InTime, Message);

	Session.UpdateDurationSeconds(InTime);
}

void FGameplayProvider::AppendWorld(uint64 InObjectId, int32 InPIEInstanceId, uint8 InType, uint8 InNetMode, bool bInIsSimulating)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;

	if (WorldIdToIndexMap.Find(InObjectId) == nullptr)
	{
		FWorldInfo NewWorldInfo;
		NewWorldInfo.Id = InObjectId;
		NewWorldInfo.PIEInstanceId = InPIEInstanceId;
		NewWorldInfo.Type = (FWorldInfo::EType)InType;
		NewWorldInfo.NetMode = (FWorldInfo::ENetMode)InNetMode;
		NewWorldInfo.bIsSimulating = bInIsSimulating;

		int32 NewWorldInfoIndex = WorldInfos.Add(NewWorldInfo);
		WorldIdToIndexMap.Add(InObjectId, NewWorldInfoIndex);
	}
}

void FGameplayProvider::AppendRecordingInfo(uint64 InWorldId, double InProfileTime, uint32 InRecordingIndex, uint32 InFrameIndex, double InElapsedTime)
{
	Session.WriteAccessCheck();

	FRecordingInfoMessage NewRecordingInfo;
	NewRecordingInfo.WorldId = InWorldId;
	NewRecordingInfo.ProfileTime = InProfileTime;
	NewRecordingInfo.RecordingIndex = InRecordingIndex;
	NewRecordingInfo.FrameIndex = InFrameIndex;
	NewRecordingInfo.ElapsedTime = InElapsedTime;

	if(TSharedRef<TraceServices::TPointTimeline<FRecordingInfoMessage>>* ExistingRecording = Recordings.Find(InRecordingIndex))
	{
		(*ExistingRecording)->AppendEvent(InProfileTime, NewRecordingInfo);
	}
	else
	{
		TSharedPtr<TraceServices::TPointTimeline<FRecordingInfoMessage>> NewRecording = MakeShared<TraceServices::TPointTimeline<FRecordingInfoMessage>>(Session.GetLinearAllocator());
		NewRecording->AppendEvent(InProfileTime, NewRecordingInfo);
		Recordings.Add(InRecordingIndex, NewRecording.ToSharedRef());
	}
}


const FGameplayProvider::RecordingInfoTimeline* FGameplayProvider::GetRecordingInfo(uint32 RecordingId) const  
{
	Session.ReadAccessCheck();

	if(const TSharedRef<TraceServices::TPointTimeline<FRecordingInfoMessage>>* Recording = Recordings.Find(RecordingId))
	{
		return &(*Recording).Get();
	}

	return nullptr;
}

void FGameplayProvider::AppendClassPropertyStringId(uint32 InStringId, const FStringView& InString)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;

	const TCHAR* StoredString = Session.StoreString(InString);

	PropertyStrings.Add(InStringId, StoredString);
}

void FGameplayProvider::AppendPropertiesStart(uint64 InObjectId, double InTime, uint64 InEventId)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;
	bHasObjectProperties = true;

	TSharedPtr<FObjectPropertiesStorage> Storage;
	uint32* IndexPtr = ObjectIdToPropertiesStorage.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		Storage = PropertiesStorage[*IndexPtr];
	}
	else
	{
		Storage = MakeShared<FObjectPropertiesStorage>();
		Storage->Timeline = MakeShared<TraceServices::TIntervalTimeline<FObjectPropertiesMessage>>(Session.GetLinearAllocator());
		ObjectIdToPropertiesStorage.Add(InObjectId, PropertiesStorage.Num());
		PropertiesStorage.Add(Storage.ToSharedRef());
	}

	Storage->OpenEventId = InEventId;
	Storage->OpenStartTime = InTime;

	FObjectPropertiesMessage& Message = Storage->OpenEvent;
	Message.PropertyValueStartIndex = Storage->Values.Num();
	Message.PropertyValueEndIndex = Storage->Values.Num();
}

void FGameplayProvider::AppendPropertiesEnd(uint64 InObjectId, double InTime)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;
	bHasObjectProperties = true;

	TSharedPtr<FObjectPropertiesStorage> Storage;
	uint32* IndexPtr = ObjectIdToPropertiesStorage.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		Storage = PropertiesStorage[*IndexPtr];
	}
	else
	{
		Storage = MakeShared<FObjectPropertiesStorage>();
		Storage->Timeline = MakeShared<TraceServices::TIntervalTimeline<FObjectPropertiesMessage>>(Session.GetLinearAllocator());
		ObjectIdToPropertiesStorage.Add(InObjectId, PropertiesStorage.Num());
		PropertiesStorage.Add(Storage.ToSharedRef());
	}

	if(Storage->OpenEventId != 0)
	{
		uint64 EventIndex = Storage->Timeline->AppendBeginEvent(Storage->OpenStartTime, Storage->OpenEvent);
		Storage->Timeline->EndEvent(EventIndex, InTime);

		Storage->OpenEventId = 0;
	}
}

void FGameplayProvider::AppendPropertyValue(uint64 InObjectId, double InTime, uint64 InEventId, int32 InParentId, uint32 InTypeStringId, uint32 InKeyStringId, const FStringView& InValue)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;
	bHasObjectProperties = true;

	TSharedPtr<FObjectPropertiesStorage> Storage;
	uint32* IndexPtr = ObjectIdToPropertiesStorage.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		Storage = PropertiesStorage[*IndexPtr];
	}
	else
	{
		Storage = MakeShared<FObjectPropertiesStorage>();
		Storage->Timeline = MakeShared<TraceServices::TIntervalTimeline<FObjectPropertiesMessage>>(Session.GetLinearAllocator());
		ObjectIdToPropertiesStorage.Add(InObjectId, PropertiesStorage.Num());
		PropertiesStorage.Add(Storage.ToSharedRef());
	}
	
	if(Storage->OpenEventId == InEventId)
	{
		FObjectPropertyValue& Message = Storage->Values.AddDefaulted_GetRef();
		Message.Value = Session.StoreString(InValue);
		Message.ValueAsFloat = FCString::Atof(Message.Value);
		Message.ParentId = InParentId;
		Message.TypeStringId = InTypeStringId;
		Message.KeyStringId = InKeyStringId; 

		Storage->OpenEvent.PropertyValueEndIndex = Storage->Values.Num();
	}
}

bool FGameplayProvider::HasAnyData() const 
{
	Session.ReadAccessCheck();

	return bHasAnyData;
}

bool FGameplayProvider::HasObjectProperties() const 
{
	Session.ReadAccessCheck();

	return bHasObjectProperties;
}

#undef LOCTEXT_NAMESPACE
