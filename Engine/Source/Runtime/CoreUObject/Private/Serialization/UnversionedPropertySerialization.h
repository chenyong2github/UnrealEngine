// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/UnrealType.h"

struct FUnversionedStructSchema;

// Check if unversioned property serialization is configured to be used
bool CanUseUnversionedPropertySerialization();

// Serialize sparse unversioned properties for a particular struct
void SerializeUnversionedProperties(const UStruct* Struct, FStructuredArchive::FSlot Slot, uint8* Data, UStruct* DefaultsStruct, uint8* Defaults);

void DestroyUnversionedSchema(const UStruct* Struct);