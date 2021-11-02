// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/MassStateTreeTestTask.h"
#include "Engine/Engine.h"
#include "StateTreeExecutionContext.h"
#include "MassStateTreeSubsystem.h"
#include "MassCommonFragments.h"
#include "MassSmartObjectProcessor.h"


FMassStateTreeTestTask::FMassStateTreeTestTask()
	: Time(0.0f)
{
}

FMassStateTreeTestTask::~FMassStateTreeTestTask()
{
}

bool FMassStateTreeTestTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalItem(MassStateTreeSubSystemHandle);
	Linker.LinkExternalItem(SmartObjectUserHandle);
	Linker.LinkExternalItem(TransformHandle);

	return true;
}

EStateTreeRunStatus FMassStateTreeTestTask::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	Time = 0.0f;
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FMassStateTreeTestTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime)
{
	UMassStateTreeSubsystem& MassStateTreeSubSystem = Context.GetExternalItem(MassStateTreeSubSystemHandle);
	const FMassSmartObjectUserFragment* SmartObjectUser = Context.GetExternalItemPtr(SmartObjectUserHandle);
	FDataFragment_Transform& Transform = Context.GetExternalItem(TransformHandle);

	GEngine->AddOnScreenDebugMessage(INDEX_NONE, 1.0f, FColor::Orange, *FString::Printf(TEXT("[%s] Time=%f X=%f\n"), *Name.ToString(), Time, Transform.GetTransform().GetLocation().X));

	Time += DeltaTime;
	return Time < Duration ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
}
