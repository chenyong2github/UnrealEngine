// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Trace/Config.h"

#if !IS_PROGRAM && !UE_BUILD_SHIPPING
#define OBJECT_TRACE_ENABLED 1
#else
#define OBJECT_TRACE_ENABLED 0
#endif

#if OBJECT_TRACE_ENABLED

class UClass;
class UObject;

struct FObjectTrace
{
	/** Initialize object tracing */
	ENGINE_API static void Init();

	/** Helper function to output an object */
	ENGINE_API static void OutputClass(const UClass* InClass);

	/** Helper function to output an object */
	ENGINE_API static void OutputObject(const UObject* InObject);

	/** Helper function to output an object event */
	ENGINE_API static void OutputObjectEvent(const UObject* InObject, const TCHAR* InEvent);

	/** Helper function to get an object ID from a UObject */
	ENGINE_API static uint64 GetObjectId(const UObject* InObject);
};

#define TRACE_CLASS(Class) \
	FObjectTrace::OutputClass(Class);

#define TRACE_OBJECT(Object) \
	FObjectTrace::OutputObject(Object);

#define TRACE_OBJECT_EVENT(Object, Event) \
	FObjectTrace::OutputObjectEvent(Object, TEXT(#Event));

#else

#define TRACE_CLASS(Class)
#define TRACE_OBJECT(Object)
#define TRACE_OBJECT_EVENT(Object, Event)

#endif