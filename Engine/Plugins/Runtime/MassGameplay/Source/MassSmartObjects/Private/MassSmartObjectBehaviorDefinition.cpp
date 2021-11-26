// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSmartObjectBehaviorDefinition.h"
#include "MassSmartObjectProcessor.h"

void USmartObjectMassBehaviorDefinition::Activate(UMassEntitySubsystem& EntitySubsystem,
                                              FMassExecutionContext& Context,
                                              const FMassBehaviorEntityContext& EntityContext) const
{
	EntityContext.SOUser.SetUseTime(UseTime);
}
