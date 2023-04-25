// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_AnimNextParameter.h"
#include "Graph/GraphExecuteContext.h"
#include "Interface/AnimNextInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_AnimNextParameter)

bool FRigUnit_AnimNextParameter::GetParameterInternal(FName InName, const FAnimNextGraphExecuteContext& InContext, void* OutResult)
{
	return true;
}

FRigUnit_AnimNextParameter_AnimSequence_Execute()
{
	const UE::AnimNext::FContext& InterfaceContext = ExecuteContext.GetContext();

	Result = InterfaceContext.GetParameterChecked<FAnimNextGraph_AnimSequence>(Parameter);

	//GetParameterInternal(Parameter, ExecuteContext, &Result);
}


FRigUnit_AnimNextParameter_AnimNextInterface_Execute()
{
	GetParameterInternal(Parameter, ExecuteContext, &Result);
}

FRigUnit_FloatOperator_Execute()
{}

FRigUnit_AnimNext_SequencePlayer_Execute()
{
}

FRigUnit_TestFloatState_Execute()
{
	using namespace UE::AnimNext;

	// @TODO: ensure that context arg is always present to avoid these checks here
	const FContext& InterfaceContext = ExecuteContext.GetContext();

	FRigUnit_TestFloatState_SpringDamperState& State = InterfaceContext.GetState<FRigUnit_TestFloatState_SpringDamperState>(ExecuteContext.GetInterface(), 0);
	const float DeltaTime = InterfaceContext.GetDeltaTime();

	FMath::SpringDamperSmoothing(
		State.Value,
		State.ValueRate,
		TargetValue,
		TargetValueRate,
		DeltaTime,
		SmoothingTime,
		DampingRatio);

	Result = State.Value;
}
