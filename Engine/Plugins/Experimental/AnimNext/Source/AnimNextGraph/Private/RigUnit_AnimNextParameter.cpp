// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_AnimNextParameter.h"
#include "UnitContext.h"
#include "ExecuteContext.h"
#include "Interface/AnimNextInterface.h"
#include "Interface/InterfaceTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_AnimNextParameter)

bool FRigUnit_AnimNextParameter::GetParameterInternal(FName InName, const FAnimNextExecuteContext& InContext, void* OutResult)
{
	return true;
}

FRigUnit_AnimNextParameter_Float_Execute()
{
	GetParameterInternal(Parameter, ExecuteContext, &Result);
}

FRigUnit_AnimNextParameter_GraphLODPose_Execute()
{
	const UE::AnimNext::FContext& InterfaceContext = ExecuteContext.GetContext();

	Result = InterfaceContext.GetParameterChecked<FAnimNextGraphLODPose>(Parameter);

	//GetParameterInternal(Parameter, ExecuteContext, &Result);
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

FRigUnit_AnimNext_Float_Execute()
{
	using namespace UE::AnimNext;

	// @TODO: ensure that context arg is always present to avoid these checks here
	const FContext& InterfaceContext = ExecuteContext.GetContext();

	// Wrap the internal result we are going to be writing to 
	TWrapParam<float> CallResult(&Result);

	// Call the interface
	ExecuteContext.SetResult(UE::AnimNext::Interface::GetDataSafe(AnimNextInterface, InterfaceContext, CallResult));
}
/*
FRigUnit_AnimNext_Pose_Execute()
{
	using namespace UE::AnimNext;

	// @TODO: ensure that context arg is always present to avoid these checks here
	if(Context.State == EControlRigState::Update && ExecuteContext.OpaqueArguments.Num() > 1 && ExecuteContext.OpaqueArguments[1] != nullptr)
	{
		const FAnimNextUnitContext& UnitContext = *static_cast<const FAnimNextUnitContext*>(ExecuteContext.OpaqueArguments[1]);
		const FContext& AnimNextContext = AnimNextUnitContext.InterfaceContext;

		// Wrap the internal result we are going to be writing to 
		TWrapParam<FAnimNextExecuteContext> CallResult(&Result);

		// Call the interface
		AnimNextInterfaceUnitContext.bResult &= UE::AnimNext::Interface::GetDataSafe(PoseInterface, AnimNextInterfaceContext, CallResult);
	}
}
*/
FRigUnit_FloatOperator_Execute()
{}

FRigUnit_PoseOperator_Execute()
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
