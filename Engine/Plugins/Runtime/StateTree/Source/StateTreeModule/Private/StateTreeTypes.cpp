// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeTypes)

DEFINE_LOG_CATEGORY(LogStateTree);

const FStateTreeStateHandle FStateTreeStateHandle::Invalid = FStateTreeStateHandle();
const FStateTreeStateHandle FStateTreeStateHandle::Succeeded = FStateTreeStateHandle(FStateTreeStateHandle::SucceededIndex);
const FStateTreeStateHandle FStateTreeStateHandle::Failed = FStateTreeStateHandle(FStateTreeStateHandle::FailedIndex);

const FStateTreeIndex16 FStateTreeIndex16::Invalid = FStateTreeIndex16();
const FStateTreeIndex8 FStateTreeIndex8::Invalid = FStateTreeIndex8();

const FStateTreeExternalDataHandle FStateTreeExternalDataHandle::Invalid = FStateTreeExternalDataHandle();


bool FStateTreeIndex16::SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot)
{
	// Support loading from Index8.
	if (Tag.Type == NAME_StructProperty
		&& Tag.StructName == FStateTreeIndex8::StaticStruct()->GetFName())
	{
		FStateTreeIndex8 OldValue;
		FStateTreeIndex8::StaticStruct()->SerializeItem(Slot, &OldValue, nullptr);

		int32 NewValue = OldValue.AsInt32();
		if (!IsValidIndex(NewValue))
		{
			NewValue = INDEX_NONE;
		}
		
		*this = FStateTreeIndex16(NewValue);
		
		return true;
	}
	
	return false;
}

bool FStateTreeIndex8::SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot)
{
	// Support loading from Index16.
	if (Tag.Type == NAME_StructProperty
		&& Tag.StructName == FStateTreeIndex16::StaticStruct()->GetFName())
	{
		FStateTreeIndex16 OldValue;
		FStateTreeIndex16::StaticStruct()->SerializeItem(Slot, &OldValue, nullptr);

		int32 NewValue = OldValue.AsInt32();
		if (!IsValidIndex(NewValue))
		{
			NewValue = INDEX_NONE;
		}
		
		*this = FStateTreeIndex8(NewValue);
		
		return true;
	}
	
	return false;
}
