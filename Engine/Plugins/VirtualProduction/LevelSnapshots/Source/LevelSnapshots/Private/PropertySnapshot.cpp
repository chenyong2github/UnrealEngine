// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertySnapshot.h"

FLevelSnapshot_Property::FLevelSnapshot_Property()
{

}

FLevelSnapshot_Property::FLevelSnapshot_Property(FProperty* InProperty, uint32 InPropertyDepth)
	: PropertyPath(InProperty)
	, PropertyFlags(InProperty ? (uint64)InProperty->GetPropertyFlags() : 0)
	, PropertyDepth(InPropertyDepth)
	, DataOffset((uint32)INDEX_NONE)
	, DataSize(0)
{

}

void FLevelSnapshot_Property::AppendSerializedData(const uint32 InOffset, const uint32 InSize)
{
	// There might be hole in the scope that a property covers
	// and so we can rely on the following check
	// check(DataOffset == (uint32)INDEX_NONE || DataOffset + DataSize == InOffset);
	// hence why the DataSize needs to be recalculated 
	DataOffset = FMath::Min(DataOffset, InOffset);
	DataSize = InOffset + InSize - DataOffset;
}

void FLevelSnapshot_Property::AddNameReference(const uint32 InOffset, const uint32 InNameIndex)
{
	ReferencedNamesOffsetToIndex.Add(InOffset, InNameIndex);
}

void FLevelSnapshot_Property::AddObjectReference(const uint32 InOffset, const uint32 InObjectIndex)
{
	ReferencedObjectOffsetToIndex.Add(InOffset, InObjectIndex);
}