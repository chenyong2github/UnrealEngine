// Copyright Epic Games, Inc. All Rights Reserved.

#include "StructUtilsTypes.h"
#include "UObject/Class.h"
#include "Serialization/ArchiveCrc32.h"
#include "InstancedStruct.h"

namespace UE::StructUtils
{
	STRUCTUTILS_API void AddStructReferencedObjects(const FConstStructView& StructView, class FReferenceCollector& Collector)
	{
		if (const UScriptStruct* ScriptStructPtr = StructView.GetScriptStruct())
		{
			Collector.AddReferencedObject(ScriptStructPtr);

			if (uint8* StructMemory = const_cast<uint8*>(StructView.GetMemory()))
			{
				if (ScriptStructPtr->StructFlags & STRUCT_AddStructReferencedObjects)
				{
					ScriptStructPtr->GetCppStructOps()->AddStructReferencedObjects()(StructMemory, Collector);
				}
				else
				{
					// The iterator will recursively loop through all structs in structs too.
					for (TPropertyValueIterator<const FObjectProperty> It(ScriptStructPtr, StructMemory); It; ++It)
					{
						UObject** ObjectPtr = static_cast<UObject**>(const_cast<void*>(It.Value()));
						Collector.AddReferencedObject(*ObjectPtr);
					}
				}
			}
		}
	}

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

	STRUCTUTILS_API uint32 GetStructCrc32(const FConstStructView& StructView, const uint32 CRC /*= 0*/)
	{
		if (const UScriptStruct* ScriptStruct = StructView.GetScriptStruct())
		{
			return GetStructCrc32(*ScriptStruct, StructView.GetMemory(), CRC);
		}
		return 0;
	}

}
