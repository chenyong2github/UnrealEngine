// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeTypes.h"

DEFINE_LOG_CATEGORY(LogStateTree);

const FStateTreeHandle FStateTreeHandle::Invalid = FStateTreeHandle();
const FStateTreeHandle FStateTreeHandle::Succeeded = FStateTreeHandle(FStateTreeHandle::SucceededIndex);
const FStateTreeHandle FStateTreeHandle::Failed = FStateTreeHandle(FStateTreeHandle::FailedIndex);

const FStateTreeExternalDataHandle FStateTreeExternalDataHandle::Invalid = FStateTreeExternalDataHandle();
