// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeTypes.h"
#include "StateTree.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeTypes)

DEFINE_LOG_CATEGORY(LogStateTree);

const FStateTreeStateHandle FStateTreeStateHandle::Invalid = FStateTreeStateHandle();
const FStateTreeStateHandle FStateTreeStateHandle::Succeeded = FStateTreeStateHandle(FStateTreeStateHandle::SucceededIndex);
const FStateTreeStateHandle FStateTreeStateHandle::Failed = FStateTreeStateHandle(FStateTreeStateHandle::FailedIndex);
const FStateTreeStateHandle FStateTreeStateHandle::Stopped = FStateTreeStateHandle(FStateTreeStateHandle::StoppedIndex);
const FStateTreeStateHandle FStateTreeStateHandle::Root = FStateTreeStateHandle(0);

const FStateTreeIndex16 FStateTreeIndex16::Invalid = FStateTreeIndex16();
const FStateTreeIndex8 FStateTreeIndex8::Invalid = FStateTreeIndex8();

const FStateTreeExternalDataHandle FStateTreeExternalDataHandle::Invalid = FStateTreeExternalDataHandle();

#if WITH_STATETREE_DEBUGGER
const FStateTreeInstanceDebugId FStateTreeInstanceDebugId::Invalid = FStateTreeInstanceDebugId();
#endif // WITH_STATETREE_DEBUGGER

//////////////////////////////////////////////////////////////////////////
// FStateTreeStateLink

bool FStateTreeStateLink::Serialize(FStructuredArchive::FSlot Slot)
{
	Slot.GetUnderlyingArchive().UsingCustomVersion(FStateTreeCustomVersion::GUID);
	return false; // Let the default serializer handle serializing.
}

void FStateTreeStateLink::PostSerialize(const FArchive& Ar)
{
#if WITH_EDITORONLY_DATA
	const int32 CurrentVersion = Ar.CustomVer(FStateTreeCustomVersion::GUID);
	if (CurrentVersion < FStateTreeCustomVersion::AddedExternalTransitions)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		LinkType = Type_DEPRECATED;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
#endif // WITH_EDITORONLY_DAT
}
