// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_AnimNextInterfaceParameter.h"
#include "AnimNextInterfaceUnitContext.h"
#include "AnimNextInterfaceExecuteContext.h"
#include "AnimNextInterface.h"
#include "AnimNextInterfaceTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_AnimNextInterfaceParameter)

bool FRigUnit_AnimNextInterfaceParameter::GetParameterInternal(FName InName, const FAnimNextInterfaceExecuteContext& InContext, void* OutResult)
{
	return true;
}

FRigUnit_AnimNextInterfaceParameter_Float_Execute()
{
	GetParameterInternal(Parameter, ExecuteContext, &Result);
}

FRigUnit_AnimNextInterfaceParameter_GraphLODPose_Execute()
{
	const UE::AnimNext::FContext& AnimNextInterfaceContext = ExecuteContext.GetContext();

	Result = AnimNextInterfaceContext.GetParameterChecked<FAnimNextGraphLODPose>(Parameter);

	//GetParameterInternal(Parameter, ExecuteContext, &Result);
}

FRigUnit_AnimNextInterfaceParameter_AnimSequence_Execute()
{
	const UE::AnimNext::FContext& AnimNextInterfaceContext = ExecuteContext.GetContext();

	Result = AnimNextInterfaceContext.GetParameterChecked<FAnimNextGraph_AnimSequence>(Parameter);

	//GetParameterInternal(Parameter, ExecuteContext, &Result);
}


FRigUnit_AnimNextInterfaceParameter_AnimNextInterface_Execute()
{
	GetParameterInternal(Parameter, ExecuteContext, &Result);
}

FRigUnit_AnimNextInterface_Float_Execute()
{
	using namespace UE::AnimNext;

	// @TODO: ensure that context arg is always present to avoid these checks here
	const FContext& AnimNextInterfaceContext = ExecuteContext.GetContext();

	// Wrap the internal result we are going to be writing to 
	TWrapParam<float> CallResult(&Result);

	// Call the interface
	ExecuteContext.SetResult(UE::AnimNext::Interface::GetDataSafe(AnimNextInterface, AnimNextInterfaceContext, CallResult));
}
/*
FRigUnit_AnimNextInterface_Pose_Execute()
{
	using namespace UE::AnimNext;

	// @TODO: ensure that context arg is always present to avoid these checks here
	if(Context.State == EControlRigState::Update && ExecuteContext.OpaqueArguments.Num() > 1 && ExecuteContext.OpaqueArguments[1] != nullptr)
	{
		const FAnimNextInterfaceUnitContext& AnimNextInterfaceUnitContext = *static_cast<const FAnimNextInterfaceUnitContext*>(ExecuteContext.OpaqueArguments[1]);
		const FContext& AnimNextInterfaceContext = AnimNextInterfaceUnitContext.AnimNextInterfaceContext;

		// Wrap the internal result we are going to be writing to 
		TWrapParam<FAnimNextInterfaceExecuteContext> CallResult(&Result);

		// Call the interface
		AnimNextInterfaceUnitContext.bResult &= UE::AnimNext::Interface::GetDataSafe(PoseInterface, AnimNextInterfaceContext, CallResult);
	}
}
*/
FRigUnit_FloatOperator_Execute()
{}

FRigUnit_PoseOperator_Execute()
{}

FRigUnit_AnimNextInterface_SequencePlayer_Execute()
{
}



FRigUnit_TestFloatState_Execute()
{
	using namespace UE::AnimNext;

	// @TODO: ensure that context arg is always present to avoid these checks here
	const FContext& AnimNextInterfaceContext = ExecuteContext.GetContext();

	FRigUnit_TestFloatState_SpringDamperState& State = AnimNextInterfaceContext.GetState<FRigUnit_TestFloatState_SpringDamperState>(ExecuteContext.GetInterface(), 0);
	const float DeltaTime = AnimNextInterfaceContext.GetDeltaTime();

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
