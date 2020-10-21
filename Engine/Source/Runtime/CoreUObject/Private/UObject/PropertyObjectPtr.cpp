// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/PropertyHelper.h"
#include "UObject/UnrealType.h"

/*-----------------------------------------------------------------------------
	FObjectPtrProperty.
-----------------------------------------------------------------------------*/
IMPLEMENT_FIELD(FObjectPtrProperty)

FString FObjectPtrProperty::GetCPPType(FString* ExtendedTypeText/*=NULL*/, uint32 CPPExportFlags/*=0*/) const
{
	return FString::Printf(TEXT("TObjectPtr<%s%s>"), PropertyClass->GetPrefixCPP(), *PropertyClass->GetName());
}
FString FObjectPtrProperty::GetCPPMacroType(FString& ExtendedTypeText) const
{
	ExtendedTypeText = FString::Printf(TEXT("TObjectPtr<%s%s>"), PropertyClass->GetPrefixCPP(), *PropertyClass->GetName());
	return TEXT("OBJECTPTR");
}

FName FObjectPtrProperty::GetID() const
{
	return NAME_ObjectPtrProperty;
}

void FObjectPtrProperty::SerializeItem(FStructuredArchive::FSlot Slot, void* Value, void const* Defaults) const
{
	FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();

	FObjectPtr* ObjectPtr = GetPropertyValuePtr(Value);

	Slot << *ObjectPtr;
}

EConvertFromTypeResult FObjectPtrProperty::ConvertFromType(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot, uint8* Data, UStruct* DefaultsStruct)
{
	// Convert raw object reference to TObjectPtr
	if (Tag.Type == NAME_ObjectProperty)
	{
		uint8* DestAddress = ContainerPtrToValuePtr<uint8>(Data, Tag.ArrayIndex);
		FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();
		FObjectPtr* ObjectPtr = GetPropertyValuePtr(DestAddress);
		Slot << *ObjectPtr;

		return UnderlyingArchive.IsCriticalError() ? EConvertFromTypeResult::CannotConvert : EConvertFromTypeResult::Converted;
	}

	return EConvertFromTypeResult::UseSerializeItem;
}

bool FObjectPtrProperty::SameType(const FProperty* Other) const
{
	// @TODO: OBJPTR: Should this be done through a new, separate API on FProperty (eg: ImplicitConv)
	return Super::SameType(Other) ||
		(Other && Other->IsA<FObjectProperty>() &&
			(PropertyClass == ((FObjectPropertyBase*)Other)->PropertyClass) );
}

bool FObjectPtrProperty::Identical(const void* A, const void* B, uint32 PortFlags) const
{
	FObjectPtr ObjectA = A ? *((FObjectPtr*)A) : FObjectPtr();
	FObjectPtr ObjectB = B ? *((FObjectPtr*)B) : FObjectPtr();

	if (ObjectA.IsNull() || ObjectB.IsNull())
	{
		return ObjectA.IsNull() == ObjectB.IsNull();
	}

	// Compare actual pointers. We don't do this during PIE because we want to be sure to serialize everything. An example is the LevelScriptActor being serialized against its CDO,
	// which contains actor references. We want to serialize those references so they are fixed up.
	const bool bDuplicatingForPIE = (PortFlags&PPF_DuplicateForPIE) != 0;
	bool bResult = !bDuplicatingForPIE ? (ObjectA == ObjectB) : false;
	// always serialize the cross level references, because they could be NULL
	// @todo: okay, this is pretty hacky overall - we should have a PortFlag or something
	// that is set during SavePackage. Other times, we don't want to immediately return false
	// (instead of just this ExportDefProps case)
	// instance testing
	if (!bResult && ObjectA->GetClass() == ObjectB->GetClass())
	{
		bool bPerformDeepComparison = (PortFlags&PPF_DeepComparison) != 0;
		if ((PortFlags&PPF_DeepCompareInstances) && !bPerformDeepComparison)
		{
			bPerformDeepComparison = ObjectA->IsTemplate() != ObjectB->IsTemplate();
		}

		if (!bResult && bPerformDeepComparison)
		{
			// In order for deep comparison to be match they both need to have the same name and that name needs to be included in the instancing table for the class
			if (ObjectA->GetFName() == ObjectB->GetFName() && ObjectA->GetClass()->GetDefaultSubobjectByName(ObjectA->GetFName()))
			{
				checkSlow(ObjectA->IsDefaultSubobject() && ObjectB->IsDefaultSubobject() && ObjectA->GetClass()->GetDefaultSubobjectByName(ObjectA->GetFName()) == ObjectB->GetClass()->GetDefaultSubobjectByName(ObjectB->GetFName())); // equivalent
				bResult = AreInstancedObjectsIdentical(ObjectA.Get(), ObjectB.Get(), PortFlags);
			}
		}
	}
	return bResult;
}

UObject* FObjectPtrProperty::GetObjectPropertyValue(const void* PropertyValueAddress) const
{
	return GetPropertyValue(PropertyValueAddress).Get();
}

void FObjectPtrProperty::SetObjectPropertyValue(void* PropertyValueAddress, UObject* Value) const
{
	SetPropertyValue(PropertyValueAddress, TCppType(Value));
}

bool FObjectPtrProperty::AllowCrossLevel() const
{
	return true;
}

uint32 FObjectPtrProperty::GetValueTypeHashInternal(const void* Src) const
{
	return GetTypeHash(GetPropertyValue(Src));
}

