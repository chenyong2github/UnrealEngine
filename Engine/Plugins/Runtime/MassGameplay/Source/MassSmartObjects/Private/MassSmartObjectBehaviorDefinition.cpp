// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSmartObjectBehaviorDefinition.h"

#include "MassCommandBuffer.h"
#include "MassSmartObjectFragments.h"

void USmartObjectMassBehaviorDefinition::Activate(UMassEntitySubsystem& EntitySubsystem,
												  FMassExecutionContext& Context,
												  const FMassBehaviorEntityContext& EntityContext) const
{
	FMassSmartObjectTimedBehaviorFragment TimedBehaviorFragment;
	TimedBehaviorFragment.UseTime = UseTime;
	Context.Defer().PushCommand(FCommandAddFragmentInstance(EntityContext.Entity, FConstStructView::Make(TimedBehaviorFragment)));
}
