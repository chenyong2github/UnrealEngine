// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSmartObjectBehaviorConfig.h"
#include "MassSmartObjectProcessor.h"

void USmartObjectMassBehaviorConfig::Activate(UMassEntitySubsystem& EntitySubsystem,
                                              FLWComponentSystemExecutionContext& Context,
                                              const FMassBehaviorEntityContext& EntityContext) const
{
	EntityContext.SOUser.SetUseTime(UseTime);
}
