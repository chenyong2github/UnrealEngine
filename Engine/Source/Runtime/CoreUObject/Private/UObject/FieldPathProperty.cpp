// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "UObject/FieldPathProperty.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/Package.h"
#include "UObject/UnrealTypePrivate.h"
#include "UObject/LinkerLoad.h"

// WARNING: This should always be the last include in any file that needs it (except .generated.h)
#include "UObject/UndefineUPropertyMacros.h"

IMPLEMENT_FIELD(FFieldPathProperty)

#if WITH_EDITORONLY_DATA
FFieldPathProperty::FFieldPathProperty(UField* InField)
	: FFieldPathProperty_Super(InField)
	, PropertyClass(nullptr)
{
	check(InField);
	PropertyClass = FFieldClass::GetNameToFieldClassMap().FindRef(InField->GetClass()->GetFName());
}
#endif // WITH_EDITORONLY_DATA

EConvertFromTypeResult FFieldPathProperty::ConvertFromType(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot, uint8* Data, UStruct* DefaultsStruct)
{
	// Convert UProperty object to TFieldPath
	if (Tag.Type == NAME_ObjectProperty)
	{
		FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();
		check(UnderlyingArchive.IsLoading() && UnderlyingArchive.IsPersistent());
		FLinker* Linker = UnderlyingArchive.GetLinker();
		check(Linker);

		FPackageIndex Index;
		UnderlyingArchive << Index;

		FString PropertyPathName;
		if (Index.IsImport())
		{
			PropertyPathName = Linker->GetImportPathName(Index);
		}
		else if (Index.IsExport())
		{
			PropertyPathName = Linker->GetExportPathName(Index);
		}

		FFieldPath ConvertedValue;
		ConvertedValue.Generate(*PropertyPathName);
		SetPropertyValue_InContainer(Data, ConvertedValue, Tag.ArrayIndex);
		return EConvertFromTypeResult::Converted;
	}

	return EConvertFromTypeResult::UseSerializeItem;
}

bool FFieldPathProperty::Identical( const void* A, const void* B, uint32 PortFlags ) const
{
	const FFieldPath ValueA = GetPropertyValue(A);
	if (B)
	{
		const FFieldPath ValueB = GetPropertyValue(B);
		return ValueA.IsPathIdentical(ValueB);
	}

	return ValueA.IsEmpty();
}

void FFieldPathProperty::SerializeItem(FStructuredArchive::FSlot Slot, void* Value, void const* Defaults) const
{
	FFieldPath* FieldPtr = GetPropertyValuePtr(Value);
	Slot << *FieldPtr;
}

void FFieldPathProperty::ExportTextItem( FString& ValueStr, const void* PropertyValue, const void* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope ) const
{
	const FFieldPath& Value = GetPropertyValue(PropertyValue);

	if (PortFlags & PPF_ExportCpp)
	{
		ValueStr += TEXT("TEXT(\"");
		ValueStr += Value.ToString();
		ValueStr += TEXT("\")");
	}
	else if (PortFlags & PPF_PropertyWindow)
	{
		if (PortFlags & PPF_Delimited)
		{
			ValueStr += TEXT("\"");
			ValueStr += Value.ToString();
			ValueStr += TEXT("\"");
		}
		else
		{
			ValueStr += Value.ToString();
		}
	}
	else
	{
		ValueStr += Value.ToString();
	}
}

const TCHAR* FFieldPathProperty::ImportText_Internal( const TCHAR* Buffer, void* Data, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText ) const
{
	FFieldPath* PathPtr = GetPropertyValuePtr(Data);
	check(PathPtr);

	if (!(PortFlags & PPF_Delimited))
	{
		PathPtr->Generate(Buffer);
		// in order to indicate that the value was successfully imported, advance the buffer past the last character that was imported
		Buffer += FCString::Strlen(Buffer);
	}
	else
	{
		FString Temp;
		Buffer = FPropertyHelpers::ReadToken(Buffer, Temp, true);
		if (!Buffer)
		{
			return nullptr;
		}

		PathPtr->Generate(*Temp);
	}

	return Buffer;
}

void FFieldPathProperty::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << PropertyClass;
}

FString FFieldPathProperty::GetCPPMacroType(FString& ExtendedTypeText) const
{
	//return FProperty::GetCPPMacroType(ExtendedTypeText);
	check(PropertyClass);
	ExtendedTypeText = FString::Printf(TEXT("TFieldPath<F%s>"), *PropertyClass->GetName());
	return TEXT("STRUCT");
}

FString FFieldPathProperty::GetCPPTypeForwardDeclaration() const
{
	check(PropertyClass);
	return FString::Printf(TEXT("class F%s;"), *PropertyClass->GetName());
}

FString FFieldPathProperty::GetCPPType(FString* ExtendedTypeText, uint32 CPPExportFlags) const
{
	checkSlow(PropertyClass);
	if (ExtendedTypeText != nullptr)
	{
		FString& InnerTypeText = *ExtendedTypeText;
		InnerTypeText = TEXT("<F");
		InnerTypeText += PropertyClass->GetName();
		InnerTypeText += TEXT(">");
	}
	return TEXT("TFieldPath");
}

#include "UObject/DefineUPropertyMacros.h"