// Copyright Epic Games, Inc. All Rights Reserved.


#include "UObject/StructVariant.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UObjectHash.h"
#include "UObject/Package.h"
#include "Serialization/PropertyLocalizationDataGathering.h"

namespace
{
#if WITH_EDITORONLY_DATA
void GatherStructVariantForLocalization(const FString& PathToParent, const UScriptStruct* Struct, const void* StructData, const void* DefaultStructData, FPropertyLocalizationDataGatherer& PropertyLocalizationDataGatherer, const EPropertyLocalizationGathererTextFlags GatherTextFlags)
{
	const FStructVariant* ThisVariant = static_cast<const FStructVariant*>(StructData);
	const FStructVariant* DefaultVariant = static_cast<const FStructVariant*>(DefaultStructData);

	PropertyLocalizationDataGatherer.GatherLocalizationDataFromStruct(PathToParent, Struct, StructData, DefaultStructData, GatherTextFlags);

	if (const UScriptStruct* StructTypePtr = ThisVariant->GetStructType())
	{
		PropertyLocalizationDataGatherer.GatherLocalizationDataFromStructWithCallbacks(PathToParent + TEXT(".StructInstance"), StructTypePtr, ThisVariant->GetStructInstance(), DefaultVariant ? DefaultVariant->GetStructInstance() : nullptr, GatherTextFlags);
	}
}
#endif // WITH_EDITORONLY_DATA
}

FStructVariant::FStructVariant()
{
#if WITH_EDITORONLY_DATA
	{ static const FAutoRegisterLocalizationDataGatheringCallback AutomaticRegistrationOfLocalizationGatherer(TBaseStructure<FStructVariant>::Get(), &GatherStructVariantForLocalization); }
#endif
}

FStructVariant::~FStructVariant()
{
	FreeStructInstance();
}

FStructVariant::FStructVariant(const FStructVariant& InOther)
{
	InitializeInstanceFrom(InOther);
}

FStructVariant& FStructVariant::operator=(const FStructVariant& InOther)
{
	if (this != &InOther)
	{
		InitializeInstanceFrom(InOther);
	}
	return *this;
}

FStructVariant::FStructVariant(FStructVariant&& InOther)
{
	InitializeInstanceFrom(MoveTemp(InOther));
}

FStructVariant& FStructVariant::operator=(FStructVariant&& InOther)
{
	if (this != &InOther)
	{
		InitializeInstanceFrom(MoveTemp(InOther));
	}
	return *this;
}

bool FStructVariant::operator==(const FStructVariant& InOther) const
{
	return Identical(&InOther, PPF_None);
}

bool FStructVariant::operator!=(const FStructVariant& InOther) const
{
	return !Identical(&InOther, PPF_None);
}

bool FStructVariant::Identical(const FStructVariant* InOther, uint32 PortFlags) const
{
	const UScriptStruct* StructTypePtr = GetStructType();
	if (StructTypePtr != InOther->GetStructType())
	{
		return false;
	}

	if (StructTypePtr)
	{
		return StructTypePtr->CompareScriptStruct(StructInstance, InOther->StructInstance, PortFlags);
	}

	return true;
}

bool FStructVariant::ExportTextItem(FString& ValueStr, const FStructVariant& DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope) const
{
	if (const UScriptStruct* StructTypePtr = GetStructType())
	{
		ValueStr += StructTypePtr->GetPathName();
		StructTypePtr->ExportText(ValueStr, StructInstance, StructTypePtr == DefaultValue.GetStructType() ? DefaultValue.StructInstance : nullptr, Parent, PortFlags, ExportRootScope);
	}
	else
	{
		ValueStr += TEXT("None");
	}
	return true;
}

bool FStructVariant::ImportTextItem(const TCHAR*& Buffer, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText)
{
	FNameBuilder StructPathName;
	if (const TCHAR* Result = FPropertyHelpers::ReadToken(Buffer, StructPathName, /*bDottedNames*/true))
	{
		Buffer = Result;
	}
	else
	{
		return false;
	}

	if (StructPathName.Len() == 0 || FCString::Stricmp(StructPathName.ToString(), TEXT("None")) == 0)
	{
		SetStructType(nullptr);
	}
	else
	{
		UScriptStruct* StructTypePtr = LoadObject<UScriptStruct>(nullptr, StructPathName.ToString());
		if (!StructTypePtr)
		{
			return false;
		}

		SetStructType(StructTypePtr);
		if (const TCHAR* Result = StructTypePtr->ImportText(Buffer, StructInstance, Parent, PortFlags, ErrorText, [StructTypePtr]() { return StructTypePtr->GetName(); }))
		{
			Buffer = Result;
		}
		else
		{
			return false;
		}
	}

	return true;
}

void FStructVariant::AddStructReferencedObjects(FReferenceCollector& Collector)
{
	if (const UScriptStruct* StructTypePtr = GetStructType())
	{
		Collector.AddReferencedObjects(StructTypePtr, StructInstance);
	}
}

const UScriptStruct* FStructVariant::GetStructType() const
{
	return StructType.Get();
}

void FStructVariant::SetStructType(const UScriptStruct* InStructType)
{
	if (GetStructType() != InStructType)
	{
		FreeStructInstance();
		StructType = InStructType;
		AllocateStructInstance();
	}
}

void* FStructVariant::GetStructInstance(const UScriptStruct* InExpectedType)
{
	if (const UScriptStruct* StructTypePtr = GetStructType())
	{
		if (!InExpectedType || StructTypePtr->IsChildOf(InExpectedType))
		{
			return StructInstance;
		}
	}
	return nullptr;
}

const void* FStructVariant::GetStructInstance(const UScriptStruct* InExpectedType) const
{
	if (const UScriptStruct* StructTypePtr = GetStructType())
	{
		if (!InExpectedType || StructTypePtr->IsChildOf(InExpectedType))
		{
			return StructInstance;
		}
	}
	return nullptr;
}

void FStructVariant::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	if (UScriptStruct* StructTypePtr = const_cast<UScriptStruct*>(GetStructType()))
	{
		OutDeps.Add(StructTypePtr);
	}
}

bool FStructVariant::Serialize(FStructuredArchive::FSlot Slot)
{
	FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();
	FStructuredArchive::FRecord Record = Slot.EnterRecord();

	// Serialize the struct type
	UScriptStruct* StructTypePtr = nullptr;
	if (UnderlyingArchive.IsSaving())
	{
		StructTypePtr = const_cast<UScriptStruct*>(GetStructType());
		Record << SA_VALUE(TEXT("StructType"), StructTypePtr);
	}
	else if (UnderlyingArchive.IsLoading())
	{
		Record << SA_VALUE(TEXT("StructType"), StructTypePtr);
		if (StructTypePtr)
		{
			UnderlyingArchive.Preload(StructTypePtr);
		}
		SetStructType(StructTypePtr);
	}

	auto SerializeStructInstance = [this, StructTypePtr, &Record]()
	{
		if (StructTypePtr)
		{
			checkf(StructInstance, TEXT("StructInstance is null! Missing call to AllocateStructInstance?"));
			StructTypePtr->SerializeItem(Record.EnterField(SA_FIELD_NAME(TEXT("StructInstance"))), StructInstance, nullptr);
		}
	};

	// Serialize the struct instance, potentially tagging it with its serialized size 
	// in-case the struct is deleted later and we need to step over the instance data
	const bool bTagStructInstance = !UnderlyingArchive.IsTextFormat();
	if (bTagStructInstance)
	{
		if (UnderlyingArchive.IsSaving())
		{
			// Write a placeholder for the serialized size
			const int64 StructInstanceSizeOffset = UnderlyingArchive.Tell();
			int64 StructInstanceSerializedSize = 0;
			UnderlyingArchive << StructInstanceSerializedSize;

			// Serialize the struct instance
			const int64 StructInstanceStartOffset = UnderlyingArchive.Tell();
			SerializeStructInstance();
			const int64 StructInstanceEndOffset = UnderlyingArchive.Tell();

			// Overwrite the placeholder with the real serialized size
			StructInstanceSerializedSize = StructInstanceEndOffset - StructInstanceStartOffset;
			UnderlyingArchive.Seek(StructInstanceSizeOffset);
			UnderlyingArchive << StructInstanceSerializedSize;
			UnderlyingArchive.Seek(StructInstanceEndOffset);
		}
		else if (UnderlyingArchive.IsLoading())
		{
			// Read the serialized size
			int64 StructInstanceSerializedSize = 0;
			UnderlyingArchive << StructInstanceSerializedSize;

			// Serialize the struct instance
			const int64 StructInstanceStartOffset = UnderlyingArchive.Tell();
			SerializeStructInstance();
			const int64 StructInstanceEndOffset = UnderlyingArchive.Tell();

			// Ensure we're at the correct location after serializing the instance data
			const int64 ExpectedStructInstanceEndOffset = StructInstanceStartOffset + StructInstanceSerializedSize;
			if (StructInstanceEndOffset != ExpectedStructInstanceEndOffset)
			{
				if (StructTypePtr)
				{
					// We only expect a mismatch here if the underlying struct is no longer available!
					UnderlyingArchive.SetCriticalError();
					UE_LOG(LogCore, Error, TEXT("FStructVariant expected to read %lld bytes for struct %s but read %lld bytes!"), StructInstanceSerializedSize, *StructTypePtr->GetName(), StructInstanceEndOffset - StructInstanceStartOffset);
				}
				UnderlyingArchive.Seek(ExpectedStructInstanceEndOffset);
			}
		}
	}
	else
	{
		SerializeStructInstance();
	}

	return true;
}

void FStructVariant::AllocateStructInstance()
{
	checkf(!StructInstance, TEXT("StructInstance was not null! Missing call to FreeStructInstance?"));
	if (const UScriptStruct* StructTypePtr = GetStructType())
	{
		StructInstance = FMemory::Malloc(FMath::Max(StructTypePtr->GetStructureSize(), 1));
		StructTypePtr->InitializeStruct(StructInstance);
	}
}

void FStructVariant::FreeStructInstance()
{
	if (const UScriptStruct* StructTypePtr = GetStructType())
	{
		StructTypePtr->DestroyStruct(StructInstance);
	}

	FMemory::Free(StructInstance);
	StructInstance = nullptr;
}

void FStructVariant::InitializeInstanceFrom(const FStructVariant& InOther)
{
	SetStructType(InOther.GetStructType());
	if (const UScriptStruct* StructTypePtr = GetStructType())
	{
		StructTypePtr->CopyScriptStruct(StructInstance, InOther.StructInstance);
	}
}

void FStructVariant::InitializeInstanceFrom(FStructVariant&& InOther)
{
	FreeStructInstance();

	StructType = InOther.StructType;
	StructInstance = InOther.StructInstance;

	InOther.StructType.Reset();
	InOther.StructInstance = nullptr;
}


UScriptStruct* TBaseStructure<FStructVariant>::Get()
{
	auto GetBaseStructureInternal = [](FName Name)
	{
		static UPackage* CoreUObjectPkg = FindObjectChecked<UPackage>(nullptr, TEXT("/Script/CoreUObject"));

		UScriptStruct* Result = (UScriptStruct*)StaticFindObjectFastInternal(UScriptStruct::StaticClass(), CoreUObjectPkg, Name, false, false, RF_NoFlags, EInternalObjectFlags::None);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (!Result)
		{
			UE_LOG(LogClass, Fatal, TEXT("Failed to find native struct '%s.%s'"), *CoreUObjectPkg->GetName(), *Name.ToString());
		}
#endif
		return Result;
	};

	static UScriptStruct* ScriptStruct = GetBaseStructureInternal(TEXT("StructVariant"));
	return ScriptStruct;
}

IMPLEMENT_STRUCT(StructVariant);
