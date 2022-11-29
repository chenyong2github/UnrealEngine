// Copyright Epic Games, Inc. All Rights Reserved.

#include "StructUtilsTypes.h"
#include "UObject/Class.h"
#include "Serialization/ArchiveCrc32.h"
#include "UObject/UObjectGlobals.h"

namespace UE::StructUtils
{
	STRUCTUTILS_API uint32 GetStructCrc32(const UScriptStruct& ScriptStruct, const uint8* StructMemory, const uint32 CRC /*= 0*/)
	{
		FArchiveCrc32 Ar(HashCombine(CRC, PointerHash(&ScriptStruct)));
		if (StructMemory)
		{
			UScriptStruct& NonConstScriptStruct = const_cast<UScriptStruct&>(ScriptStruct);
			NonConstScriptStruct.SerializeItem(Ar, const_cast<uint8*>(StructMemory), nullptr);
		}
		return Ar.GetCrc();
	}

	STRUCTUTILS_API uint32 GetStructCrc32(const FConstStructView StructView, const uint32 CRC /*= 0*/)
	{
		if (const UScriptStruct* ScriptStruct = StructView.GetScriptStruct())
		{
			return GetStructCrc32(*ScriptStruct, StructView.GetMemory(), CRC);
		}
		return 0;
	}

	STRUCTUTILS_API void AddReferencedObjects(FReferenceCollector& Collector, const UScriptStruct*& ScriptStruct, void* StructMemory,
										 const UObject* ReferencingObject, const FProperty* ReferencingProperty)
	{
		check(ScriptStruct != nullptr);
		check(StructMemory != nullptr);

		Collector.AddReferencedObject(ScriptStruct, ReferencingObject, ReferencingProperty);

		// If the script struct explicitly provided an implementation of AddReferencedObjects, make sure to capture its referenced objects
		if (ScriptStruct->StructFlags & STRUCT_AddStructReferencedObjects)
		{
			ScriptStruct->GetCppStructOps()->AddStructReferencedObjects()(StructMemory, Collector);
		}

		// Iterate through all object and struct properties within the struct (will also search through structs within the struct)
		for (TPropertyValueIterator<const FProperty> PropertyIter(ScriptStruct, StructMemory); PropertyIter; ++PropertyIter)
		{
			if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(PropertyIter.Key()))
			{
				TObjectPtr<UObject>& ObjectPtrRef = ObjectProperty->GetObjectPtrPropertyValueRef(PropertyIter.Value());
				Collector.AddReferencedObject(ObjectPtrRef, ReferencingObject, ReferencingProperty);
			}
			else if (const FStructProperty* StructProperty = CastField<FStructProperty>(PropertyIter.Key()))
			{
				const UScriptStruct* ChildScriptStruct = StructProperty->Struct; 
				if (ChildScriptStruct && ChildScriptStruct->StructFlags & STRUCT_AddStructReferencedObjects)
				{
					ChildScriptStruct->GetCppStructOps()->AddStructReferencedObjects()(const_cast<void*>(PropertyIter.Value()), Collector);
				}
			}
		}
	}

}
