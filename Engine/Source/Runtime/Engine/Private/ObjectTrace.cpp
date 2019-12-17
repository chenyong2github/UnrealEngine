// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ObjectTrace.h"

#if OBJECT_TRACE_ENABLED

#include "CoreMinimal.h"
#include "Trace/Trace.h"
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "UObject/UObjectAnnotation.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "UObject/WeakObjectPtr.h"
#include "Misc/CommandLine.h"

UE_TRACE_EVENT_BEGIN(Object, Class, Important)
	UE_TRACE_EVENT_FIELD(uint64, Id)
	UE_TRACE_EVENT_FIELD(uint64, SuperId)
	UE_TRACE_EVENT_FIELD(int32, ClassNameStringLength)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Object, Object, Important)
	UE_TRACE_EVENT_FIELD(uint64, Id)
	UE_TRACE_EVENT_FIELD(uint64, ClassId)
	UE_TRACE_EVENT_FIELD(uint64, OuterId)
	UE_TRACE_EVENT_FIELD(int32, ObjectNameStringLength)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Object, ObjectEvent)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, Id)
	UE_TRACE_EVENT_FIELD(uint8, Event)
UE_TRACE_EVENT_END()

// Object annotations used for tracing
struct FTracedObjectAnnotation
{
	FTracedObjectAnnotation()
		: bTraced(false)
	{}

	// Whether this object has been traced this session
	bool bTraced;

	/** Determine if this annotation is default - required for annotations */
	FORCEINLINE bool IsDefault() const
	{
		return !bTraced;
	}
};

// Object annotations used for tracing
FUObjectAnnotationSparse<FTracedObjectAnnotation, true> GObjectTraceAnnotations;

void FObjectTrace::Init()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("objecttrace")))
	{
		UE_TRACE_EVENT_IS_ENABLED(Object, Class);
		UE_TRACE_EVENT_IS_ENABLED(Object, Object);
		UE_TRACE_EVENT_IS_ENABLED(Object, ObjectEvent);
		Trace::ToggleEvent(TEXT("Object"), true);
	}
}

uint64 FObjectTrace::GetObjectId(const UObject* InObject)
{
	// An object ID uses a combination of its own and its outer's hash
	// We do this to represent objects that get renamed into different outers 
	// as distinct traces (we don't attempt to link them).
	// We use a weak object ptr hash to take into account object name-recycling, 
	// and represent the new object as a distinct entry
	uint64 ObjectHash = 0;
	uint64 OuterHash = 0;
	if(InObject != nullptr)
	{
		ObjectHash = GetTypeHash(TWeakObjectPtr<const UObject>(InObject));
		if(UObject* Outer = InObject->GetOuter())
		{
			OuterHash = GetTypeHash(TWeakObjectPtr<UObject>(Outer));
		}
	}

	return ObjectHash | (OuterHash << 32);
}

void FObjectTrace::OutputClass(const UClass* InClass)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Object, Class);
	if (!bEventEnabled || InClass == nullptr)
	{
		return;
	}

	FTracedObjectAnnotation Annotation = GObjectTraceAnnotations.GetAnnotation(InClass);
	if(Annotation.bTraced)
	{
		// Already traced, so skip
		return;
	}

	Annotation.bTraced = true;
	GObjectTraceAnnotations.AddAnnotation(InClass, Annotation);

	int32 ClassNameStringLength = InClass->GetFName().GetStringLength() + 1;
	int32 ClassFullNameStringLength = InClass->GetPathName().Len() + 1;

	auto StringCopyFunc = [ClassNameStringLength, ClassFullNameStringLength, InClass](uint8* Out)
	{
		InClass->GetFName().ToString(reinterpret_cast<TCHAR*>(Out), ClassNameStringLength);
		FPlatformMemory::Memcpy(reinterpret_cast<TCHAR*>(Out) + ClassNameStringLength, *InClass->GetPathName(), ClassFullNameStringLength * sizeof(TCHAR));
	};

	UE_TRACE_LOG(Object, Class, (ClassNameStringLength + ClassFullNameStringLength) * sizeof(TCHAR))
		<< Class.ClassNameStringLength(ClassNameStringLength)
		<< Class.Id(GetObjectId(InClass))
		<< Class.SuperId(GetObjectId(InClass->GetSuperClass()))
		<< Class.Attachment(StringCopyFunc);
}

void FObjectTrace::OutputObject(const UObject* InObject)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Object, Object);
	if (!bEventEnabled || InObject == nullptr)
	{
		return;
	}

	if(InObject->HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	FTracedObjectAnnotation Annotation = GObjectTraceAnnotations.GetAnnotation(InObject);
	if(Annotation.bTraced)
	{
		// Already traced, so skip
		return;
	}

	Annotation.bTraced = true;
	GObjectTraceAnnotations.AddAnnotation(InObject, Annotation);

	// Trace the object's class first
	TRACE_CLASS(InObject->GetClass());

	int32 ObjectNameStringLength = InObject->GetFName().GetStringLength() + 1;
	int32 ObjectPathNameStringLength = InObject->GetPathName().Len() + 1;

	auto StringCopyFunc = [ObjectNameStringLength, ObjectPathNameStringLength, InObject](uint8* Out)
	{
		InObject->GetFName().ToString(reinterpret_cast<TCHAR*>(Out), ObjectNameStringLength);
		FPlatformMemory::Memcpy(reinterpret_cast<TCHAR*>(Out) + ObjectNameStringLength, *InObject->GetPathName(), ObjectPathNameStringLength * sizeof(TCHAR));
	};

	UE_TRACE_LOG(Object, Object, (ObjectNameStringLength + ObjectPathNameStringLength) * sizeof(TCHAR))
		<< Object.ObjectNameStringLength(ObjectNameStringLength)
		<< Object.Id(GetObjectId(InObject))
		<< Object.ClassId(GetObjectId(InObject->GetClass()))
		<< Object.OuterId(GetObjectId(InObject->GetOuter()))
		<< Object.Attachment(StringCopyFunc);
}

void FObjectTrace::OutputObjectEvent(const UObject* InObject, const TCHAR* InEvent)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Object, ObjectEvent);
	if (!bEventEnabled || InObject == nullptr)
	{
		return;
	}

	if(InObject->HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	TRACE_OBJECT(InObject);

	int32 StringBufferSize = (FCString::Strlen(InEvent) + 1) * sizeof(TCHAR);

	UE_TRACE_LOG(Object, ObjectEvent, StringBufferSize)
		<< ObjectEvent.Cycle(FPlatformTime::Cycles64())
		<< ObjectEvent.Id(GetObjectId(InObject))
		<< ObjectEvent.Attachment(InEvent, StringBufferSize);
}

#endif