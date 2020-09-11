// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectPropertyTrace.h"
#include "Trace/Trace.h"
#include "Trace/Trace.inl"
#include "Containers/Ticker.h"

#if WITH_ENGINE

#if OBJECT_PROPERTY_TRACE_ENABLED
UE_TRACE_CHANNEL(ObjectProperties)

UE_TRACE_EVENT_BEGIN(Object, ClassPropertyStringId, Important)
	UE_TRACE_EVENT_FIELD(uint32, Id)
	UE_TRACE_EVENT_FIELD(Trace::WideString, Value)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Object, ClassProperty, Important)
	UE_TRACE_EVENT_FIELD(uint64, ClassId)
	UE_TRACE_EVENT_FIELD(int32, Id)
	UE_TRACE_EVENT_FIELD(int32, ParentId)
	UE_TRACE_EVENT_FIELD(uint32, TypeId)
	UE_TRACE_EVENT_FIELD(uint32, KeyId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Object, PropertiesStart)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, ObjectId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Object, PropertiesEnd)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, ObjectId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Object, PropertyValue)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, ObjectId)
	UE_TRACE_EVENT_FIELD(int32, PropertyId)
	UE_TRACE_EVENT_FIELD(Trace::WideString, Value)
UE_TRACE_EVENT_END()

namespace ObjectPropertyTrace
{
	static FDelegateHandle TickerHandle;
	static TArray<TWeakObjectPtr<const UObject>> Objects;

	static uint32 CurrentClassPropertyStringId = 0;
	static TMap<FString, uint32> StringIdMap;

	static TSet<uint64> TracedClassIds;

	typedef TFunctionRef<void(const FString&, const FString&, const FString&, int32, int32)> IterateFunction;

	static uint32 TraceStringId(const FString& InString)
	{
		if(uint32* ExistingIdPtr = StringIdMap.Find(InString))
		{
			return *ExistingIdPtr;
		}

		uint32 NewId = StringIdMap.Add(InString, ++CurrentClassPropertyStringId);

		UE_TRACE_LOG(Object, ClassPropertyStringId, ObjectProperties)
			<< ClassPropertyStringId.Id(NewId)
			<< ClassPropertyStringId.Value(*InString);

		return NewId;
	}

	static bool ShouldTraceClassProperties(uint64 InClassId)
	{
		if(uint64* ExistingIdPtr = TracedClassIds.Find(InClassId))
		{
			return false;
		}

		TracedClassIds.Add(InClassId);

		return true;
	}

	static void IteratePropertiesRecursive(FProperty* InProperty, const void* InContainer, const FString& InKey, IterateFunction InFunction, int32& InId, int32 InParentId)
	{
		// Handle container properties
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(InProperty))
		{
			FScriptArrayHelper_InContainer Helper(ArrayProperty, InContainer);

			int32 ArrayRootId = ++InId;
			FString SizeString = FString::Printf(TEXT("{Num = %d}"), Helper.Num());
			InFunction(InProperty->GetCPPType(), InKey, SizeString, ArrayRootId, InParentId);
			
			for (int32 DynamicIndex = 0; DynamicIndex < Helper.Num(); ++DynamicIndex)
			{
				const void* ValuePtr = Helper.GetRawPtr(DynamicIndex);
				FString KeyString = FString::Printf(TEXT("[%d]"), DynamicIndex);

				IteratePropertiesRecursive(ArrayProperty->Inner, ValuePtr, KeyString, InFunction, InId, ArrayRootId);
			}
		}
		else if (const FMapProperty* MapProperty = CastField<FMapProperty>(InProperty))
		{
			FScriptMapHelper_InContainer Helper(MapProperty, InContainer);

			int32 MapRootId = ++InId;
			FString SizeString = FString::Printf(TEXT("{Num = %d}"), Helper.Num());
			InFunction(InProperty->GetCPPType(), InKey, SizeString, MapRootId, InParentId);

			int32 Num = Helper.Num();
			int32 MapIndex = 0;
			for (int32 DynamicIndex = 0; Num; ++DynamicIndex)
			{
				if (Helper.IsValidIndex(DynamicIndex))
				{
					int32 MapEntryId = ++InId;
					FString KeyString = FString::Printf(TEXT("[%d]"), MapIndex++);
					FString TypeString = FString::Printf(TEXT("{%s, %s}"), *MapProperty->KeyProp->GetCPPType(), *MapProperty->ValueProp->GetCPPType());
					InFunction(TypeString, KeyString, TEXT("{...}"), MapEntryId, MapRootId);

					const void* KeyPtr = Helper.GetKeyPtr(DynamicIndex);
					IteratePropertiesRecursive(MapProperty->KeyProp, KeyPtr, MapProperty->KeyProp->GetName(), InFunction, InId, MapEntryId);

					const void* ValuePtr = Helper.GetValuePtr(DynamicIndex);
					IteratePropertiesRecursive(MapProperty->ValueProp, ValuePtr, MapProperty->ValueProp->GetName(), InFunction, InId, MapEntryId);

					--Num;
				}
			}
		}
		else if (const FSetProperty* SetProperty = CastField<FSetProperty>(InProperty))
		{
			FScriptSetHelper_InContainer Helper(SetProperty, InContainer);

			int32 SetRootId = ++InId;
			FString SizeString = FString::Printf(TEXT("{Num = %d}"), Helper.Num());
			InFunction(InProperty->GetCPPType(), InKey, SizeString, SetRootId, InParentId);

			int32 Num = Helper.Num();
			int32 SetIndex = 0;
			for (int32 DynamicIndex = 0; Num; ++DynamicIndex)
			{
				if (Helper.IsValidIndex(DynamicIndex))
				{
					const void* ValuePtr = Helper.GetElementPtr(DynamicIndex);
					FString KeyString = FString::Printf(TEXT("[%d]"), SetIndex++);

					IteratePropertiesRecursive(SetProperty->ElementProp, ValuePtr, KeyString, InFunction, InId, SetRootId);

					--Num;
				}
			}
		}
		else if (const FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
		{
			int32 StructRootId = ++InId;
			InFunction(InProperty->GetCPPType(), InKey, TEXT("{...}"), StructRootId, InParentId);

			// Recurse
			const void* StructContainer = StructProperty->ContainerPtrToValuePtr<const void>(InContainer);
			for(TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
			{
				IteratePropertiesRecursive(*It, StructContainer, It->GetName(), InFunction, InId, StructRootId);
			}
		}
		else
		{
			// Normal property
			int32 PropertyParentId = InParentId;
			if(InProperty->ArrayDim > 1)
			{
				// Handle static array header
				PropertyParentId = ++InId;
				FString SizeString = FString::Printf(TEXT("{Num = %d}"), InProperty->ArrayDim);
				InFunction(InProperty->GetCPPType(), InKey, SizeString, PropertyParentId, InParentId);
			}

			for (int32 StaticIndex = 0; StaticIndex != InProperty->ArrayDim; ++StaticIndex)
			{
				const void* ValuePtr = InProperty->ContainerPtrToValuePtr<const void>(InContainer, StaticIndex);

				FString KeyString = InProperty->ArrayDim == 1 ? InKey : FString::Printf(TEXT("[%d]"), StaticIndex);
				FString ValueString;
				InProperty->ExportText_Direct(ValueString, ValuePtr, ValuePtr, nullptr, PPF_None);

				InFunction(InProperty->GetCPPType(), KeyString, ValueString, ++InId, PropertyParentId);
			}
		}
	}

	static void IterateProperties(UStruct* InStruct, const void* InContainer, IterateFunction InFunction)
	{
		int32 Id = INDEX_NONE;
		for(TFieldIterator<FProperty> It(InStruct); It; ++It)
		{
			IteratePropertiesRecursive(*It, InContainer, It->GetName(), InFunction, Id, INDEX_NONE);
		}
	}

	static void TraceObjects()
	{
		for(const TWeakObjectPtr<const UObject>& WeakObject : ObjectPropertyTrace::Objects)
		{
			if(const UObject* TracedObject = WeakObject.Get())
			{
				uint64 StartCycle = FPlatformTime::Cycles64();
				uint64 ObjectId = FObjectTrace::GetObjectId(TracedObject);

				UE_TRACE_LOG(Object, PropertiesStart, ObjectProperties)
					<< PropertiesStart.Cycle(StartCycle)
					<< PropertiesStart.ObjectId(ObjectId);
				
				uint64 ClassId = FObjectTrace::GetObjectId(TracedObject->GetClass());
				const bool bTraceClassProperties = ShouldTraceClassProperties(ClassId);

				IterateProperties(TracedObject->GetClass(), TracedObject, [StartCycle, ClassId, ObjectId, bTraceClassProperties](const FString& InType, const FString& InKey, const FString& InValue, int32 InId, int32 InParentId)
				{
					if(bTraceClassProperties)
					{
						uint32 TypeId = TraceStringId(InType);
						uint32 KeyId = TraceStringId(InKey);

						UE_TRACE_LOG(Object, ClassProperty, ObjectProperties)
							<< ClassProperty.ClassId(ClassId)
							<< ClassProperty.Id(InId)
							<< ClassProperty.ParentId(InParentId)
							<< ClassProperty.TypeId(TypeId)
							<< ClassProperty.KeyId(KeyId);
					}

					UE_TRACE_LOG(Object, PropertyValue, ObjectProperties)
						<< PropertyValue.Cycle(StartCycle)
						<< PropertyValue.ObjectId(ObjectId)
						<< PropertyValue.PropertyId(InId)
						<< PropertyValue.Value(*InValue);
				});

				uint64 EndCycle = FPlatformTime::Cycles64();

				UE_TRACE_LOG(Object, PropertiesEnd, ObjectProperties)
					<< PropertiesEnd.Cycle(EndCycle)
					<< PropertiesEnd.ObjectId(ObjectId);
			}
		}
	}
}

void FObjectPropertyTrace::Init()
{
	check(!ObjectPropertyTrace::TickerHandle.IsValid());
	ObjectPropertyTrace::TickerHandle = FTicker::GetCoreTicker().AddTicker(TEXT("ObjectPropertyTrace"), 0.0f, [](float InDelta)
	{
		if(UE_TRACE_CHANNELEXPR_IS_ENABLED(ObjectProperties))
		{
			ObjectPropertyTrace::TraceObjects();
		}

		return true;
	});
}

void FObjectPropertyTrace::Destroy()
{
	check(ObjectPropertyTrace::TickerHandle.IsValid());
	FTicker::GetCoreTicker().RemoveTicker(ObjectPropertyTrace::TickerHandle);
}

bool FObjectPropertyTrace::IsEnabled()
{
	return UE_TRACE_CHANNELEXPR_IS_ENABLED(ObjectProperties);
}

void FObjectPropertyTrace::ToggleObjectRegistration(const UObject* InObject)
{
	if(IsObjectRegistered(InObject))
	{
		UnregisterObject(InObject);
	}
	else
	{
		RegisterObject(InObject);
	}
}

void FObjectPropertyTrace::RegisterObject(const UObject* InObject)
{
	ObjectPropertyTrace::Objects.AddUnique(InObject);
}

void FObjectPropertyTrace::UnregisterObject(const UObject* InObject)
{
	ObjectPropertyTrace::Objects.Remove(InObject);
}

bool FObjectPropertyTrace::IsObjectRegistered(const UObject* InObject)
{
	return ObjectPropertyTrace::Objects.Contains(InObject);
}

#endif

#endif