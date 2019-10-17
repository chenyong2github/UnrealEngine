// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*-----------------------------------------------------------------------------
	FPropertyTag.
-----------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"

/**
 *  A tag describing a class property, to aid in serialization.
 */
struct FPropertyTag
{
	// Transient.
	UProperty* Prop = nullptr;

	// Variables.
	FName	Type;		// Type of property
	uint8	BoolVal = 0;// a boolean property's value (never need to serialize data for bool properties except here)
	FName	Name;		// Name of property.
	FName	StructName;	// Struct name if UStructProperty.
	FName	EnumName;	// Enum name if UByteProperty or UEnumProperty
	FName	InnerType;	// Inner type if UArrayProperty, USetProperty, or UMapProperty
	FName	ValueType;	// Value type if UMapPropery
	int32	Size = 0;   // Property size.
	int32	ArrayIndex = INDEX_NONE; // Index if an array; else 0.
	int64	SizeOffset = INDEX_NONE; // location in stream of tag size member
	FGuid	StructGuid;
	uint8	HasPropertyGuid = 0;
	FGuid	PropertyGuid;

	// Constructors.
	FPropertyTag() {}
	FPropertyTag( FArchive& InSaveAr, UProperty* Property, int32 InIndex, uint8* Value, uint8* Defaults );

	// Set optional property guid
	void SetPropertyGuid(const FGuid& InPropertyGuid);

	// Serializer.
	friend FArchive& operator<<(FArchive& Ar, FPropertyTag& Tag);
	friend void operator<<(FStructuredArchive::FSlot Slot, FPropertyTag& Tag);

	// Property serializer.
	void SerializeTaggedProperty(FArchive& Ar, UProperty* Property, uint8* Value, uint8* Defaults) const;
	void SerializeTaggedProperty(FStructuredArchive::FSlot Slot, UProperty* Property, uint8* Value, uint8* Defaults) const;
};
