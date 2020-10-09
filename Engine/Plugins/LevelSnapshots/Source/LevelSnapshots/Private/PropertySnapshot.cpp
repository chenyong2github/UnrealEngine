// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertySnapshot.h"

FInternalPropertySnapshot::FInternalPropertySnapshot()
{

}

FInternalPropertySnapshot::FInternalPropertySnapshot(FProperty* InProperty, uint32 InPropertyDepth)
	: PropertyPath(InProperty)
	, PropertyFlags(InProperty ? (uint64)InProperty->GetPropertyFlags() : 0)
	, PropertyDepth(InPropertyDepth)
	, DataOffset((uint32)INDEX_NONE)
	, DataSize(0)
{

}

void FInternalPropertySnapshot::AppendSerializedData(const uint32 InOffset, const uint32 InSize)
{
	// There might be hole in the scope that a property covers
	// and so we can rely on the following check
	// check(DataOffset == (uint32)INDEX_NONE || DataOffset + DataSize == InOffset);
	// hence why the DataSize needs to be recalculated 
	DataOffset = FMath::Min(DataOffset, InOffset);
	DataSize = InOffset + InSize - DataOffset;
}

void FInternalPropertySnapshot::AddNameReference(const uint32 InOffset, const uint32 InNameIndex)
{
	ReferencedNamesOffsetToIndex.Add(InOffset, InNameIndex);
}

void FInternalPropertySnapshot::AddObjectReference(const uint32 InOffset, const uint32 InObjectIndex)
{
	ReferencedObjectOffsetToIndex.Add(InOffset, InObjectIndex);
}