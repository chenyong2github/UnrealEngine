// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "UObject/PropertyTag.h"
#include "UObject/DebugSerializationFlags.h"
#include "Serialization/SerializedPropertyScope.h"
#include "Serialization/ArchiveUObjectFromStructuredArchive.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "UObject/BlueprintsObjectVersion.h"

/*-----------------------------------------------------------------------------
FPropertyTag
-----------------------------------------------------------------------------*/

FPropertyTag::FPropertyTag( FArchive& InSaveAr, UProperty* Property, int32 InIndex, uint8* Value, uint8* Defaults )
	: Prop      (Property)
	, Type      (Property->GetID())
	, Name      (Property->GetFName())
	, ArrayIndex(InIndex)
{
	check(!InSaveAr.GetArchiveState().UseUnversionedPropertySerialization());
	if (Property)
	{
		// Handle structs.
		if (UStructProperty* StructProperty = Cast<UStructProperty>(Property))
		{
			StructName = StructProperty->Struct->GetFName();
			StructGuid = StructProperty->Struct->GetCustomGuid();
		}
		else if (UEnumProperty* EnumProp = Cast<UEnumProperty>(Property))
		{
			if (UEnum* Enum = EnumProp->GetEnum())
			{
				EnumName = Enum->GetFName();
			}
		}
		else if (UByteProperty* ByteProp = Cast<UByteProperty>(Property))
		{
			if (ByteProp->Enum != nullptr)
			{
				EnumName = ByteProp->Enum->GetFName();
			}
		}
		else if (UArrayProperty* ArrayProp = Cast<UArrayProperty>(Property))
		{
			InnerType = ArrayProp->Inner->GetID();
		}
		else if (USetProperty* SetProp = Cast<USetProperty>(Property))
		{
			InnerType = SetProp->ElementProp->GetID();
		}
		else if (UMapProperty* MapProp = Cast<UMapProperty>(Property))
		{
			InnerType = MapProp->KeyProp->GetID();
			ValueType = MapProp->ValueProp->GetID();
		}
		else if (UBoolProperty* Bool = Cast<UBoolProperty>(Property))
		{
			BoolVal = Bool->GetPropertyValue(Value);
		}
	}
}

// Set optional property guid
void FPropertyTag::SetPropertyGuid(const FGuid& InPropertyGuid)
{
	if (InPropertyGuid.IsValid())
	{
		PropertyGuid = InPropertyGuid;
		HasPropertyGuid = true;
	}
}

// Serializer.
FArchive& operator<<(FArchive& Ar, FPropertyTag& Tag)
{
	FStructuredArchiveFromArchive(Ar).GetSlot() << Tag;
	return Ar;
}

void operator<<(FStructuredArchive::FSlot Slot, FPropertyTag& Tag)
{
	FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();
	bool bIsTextFormat = UnderlyingArchive.IsTextFormat();

	int32 Version = UnderlyingArchive.UE4Ver();

	check(!UnderlyingArchive.GetArchiveState().UseUnversionedPropertySerialization());
	checkf(!UnderlyingArchive.IsSaving() || Tag.Prop, TEXT("FPropertyTag must be constructed with a valid property when used for saving data!"));

	if (!bIsTextFormat)
	{
		// Name.
		Slot << SA_ATTRIBUTE(TEXT("Name"), Tag.Name);
		if (Tag.Name.IsNone())
		{
			return;
		}
	}

	Slot << SA_ATTRIBUTE(TEXT("Type"), Tag.Type);

	if (UnderlyingArchive.IsSaving())
	{
		// remember the offset of the Size variable - UStruct::SerializeTaggedProperties will update it after the
		// property has been serialized.
		Tag.SizeOffset = UnderlyingArchive.Tell();
	}

	if (!bIsTextFormat)
	{
		FArchive::FScopeSetDebugSerializationFlags S(UnderlyingArchive, DSF_IgnoreDiff);
		Slot << SA_ATTRIBUTE(TEXT("Size"), Tag.Size);
		Slot << SA_ATTRIBUTE(TEXT("ArrayIndex"), Tag.ArrayIndex);
	}

	if (Tag.Type.GetNumber() == 0)
	{
		FNameEntryId TagType = Tag.Type.GetComparisonIndex();

		// only need to serialize this for structs
		if (TagType == NAME_StructProperty)
		{
			Slot << SA_ATTRIBUTE(TEXT("StructName"), Tag.StructName);
			if (Version >= VER_UE4_STRUCT_GUID_IN_PROPERTY_TAG)
			{
				if (bIsTextFormat)
				{
					Slot << SA_OPTIONAL_ATTRIBUTE(TEXT("StructGuid"), Tag.StructGuid, FGuid());
				}
				else
				{
					Slot << SA_ATTRIBUTE(TEXT("StructGuid"), Tag.StructGuid);
				}
			}
		}
		// only need to serialize this for bools
		else if (TagType == NAME_BoolProperty && !UnderlyingArchive.IsTextFormat())
		{
			if (UnderlyingArchive.IsSaving())
			{
				FSerializedPropertyScope SerializedProperty(UnderlyingArchive, Tag.Prop);
				Slot << SA_ATTRIBUTE(TEXT("BoolVal"), Tag.BoolVal);
			}
			else
			{
				Slot << SA_ATTRIBUTE(TEXT("BoolVal"), Tag.BoolVal);
			}
		}
		// only need to serialize this for bytes/enums
		else if (TagType == NAME_ByteProperty)
		{
			if (UnderlyingArchive.IsTextFormat())
			{
				Slot << SA_OPTIONAL_ATTRIBUTE(TEXT("EnumName"), Tag.EnumName, NAME_None);
			}
			else
			{
				Slot << SA_ATTRIBUTE(TEXT("EnumName"), Tag.EnumName);
			}
		}
		else if (TagType == NAME_EnumProperty)
		{
			Slot << SA_ATTRIBUTE(TEXT("EnumName"), Tag.EnumName);
		}
		// only need to serialize this for arrays
		else if (TagType == NAME_ArrayProperty)
		{
			if (Version >= VAR_UE4_ARRAY_PROPERTY_INNER_TAGS)
			{
				Slot << SA_ATTRIBUTE(TEXT("InnerType"), Tag.InnerType);
			}
		}
		else if (Version >= VER_UE4_PROPERTY_TAG_SET_MAP_SUPPORT)
		{
			if (TagType == NAME_SetProperty)
			{
				Slot << SA_ATTRIBUTE(TEXT("InnerType"), Tag.InnerType);
			}
			else if (TagType == NAME_MapProperty)
			{
				Slot << SA_ATTRIBUTE(TEXT("InnerType"), Tag.InnerType);
				Slot << SA_ATTRIBUTE(TEXT("ValueType"), Tag.ValueType);
			}
		}
	}

	// Property tags to handle renamed blueprint properties effectively.
	if (Version >= VER_UE4_PROPERTY_GUID_IN_PROPERTY_TAG)
	{
		if (bIsTextFormat)
		{
			Slot << SA_OPTIONAL_ATTRIBUTE(TEXT("PropertyGuid"), Tag.PropertyGuid, FGuid());
			Tag.HasPropertyGuid = Tag.PropertyGuid.IsValid();
		}
		else
		{
			Slot << SA_ATTRIBUTE(TEXT("HasPropertyGuid"), Tag.HasPropertyGuid);
			if (Tag.HasPropertyGuid)
			{
				Slot << SA_ATTRIBUTE(TEXT("PropertyGuid"), Tag.PropertyGuid);
			}
		}
	}
}

// Property serializer.
void FPropertyTag::SerializeTaggedProperty(FArchive& Ar, UProperty* Property, uint8* Value, uint8* Defaults) const
{
	SerializeTaggedProperty(FStructuredArchiveFromArchive(Ar).GetSlot(), Property, Value, Defaults);
}

void FPropertyTag::SerializeTaggedProperty(FStructuredArchive::FSlot Slot, UProperty* Property, uint8* Value, uint8* Defaults) const
{
	FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();

	if (!UnderlyingArchive.IsTextFormat() && Property->GetClass() == UBoolProperty::StaticClass())
	{
		// ensure that the property scope gets recorded for boolean properties even though the data is stored in the tag
		FSerializedPropertyScope SerializedProperty(UnderlyingArchive, Property);
		UnderlyingArchive.Serialize(nullptr, 0); 

		UBoolProperty* Bool = (UBoolProperty*)Property;
		if (UnderlyingArchive.IsLoading())
		{
			Bool->SetPropertyValue(Value, BoolVal != 0);
		}

		Slot.EnterStream();	// Effectively discard
	}
	else
	{
#if WITH_EDITOR
		static const FName NAME_SerializeTaggedProperty = FName(TEXT("SerializeTaggedProperty"));
		FArchive::FScopeAddDebugData P(UnderlyingArchive, NAME_SerializeTaggedProperty);
		FArchive::FScopeAddDebugData A(UnderlyingArchive, Property->GetFName());
#endif
		FSerializedPropertyScope SerializedProperty(UnderlyingArchive, Property);

		Property->SerializeItem(Slot, Value, Defaults);
	}
}
