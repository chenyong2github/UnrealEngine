// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_DataInterfaceParameter.h"
#include "DataInterfaceUnitContext.h"
#include "DataInterfaceExecuteContext.h"
#include "DataInterface.h"
#include "DataInterfaceTypes.h"
#include "DataInterfaceKernel.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_DataInterfaceParameter)

bool FRigUnit_DataInterfaceParameter::GetParameterInternal(FName InName, const FDataInterfaceUnitContext& InContext, void* OutResult)
{
	return true;
}

FRigUnit_DataInterfaceParameter_Float_Execute()
{
	GetParameterInternal(Parameter, ExecuteContext.GetUnitContext(), &Result);
}

FRigUnit_DataInterfaceParameter_DataInterface_Execute()
{
	GetParameterInternal(Parameter, ExecuteContext.GetUnitContext(), &Result);
}

FRigUnit_DataInterface_Float_Execute()
{
	using namespace UE::DataInterface;

	// @TODO: ensure that context arg is always present to avoid these checks here
	const FContext& DataInterfaceContext = ExecuteContext.GetContext();

	// Wrap the internal result we are going to be writing to 
	TWrapParam<float> CallResult(&Result);

	// Call the interface
	ExecuteContext.SetResult(UE::DataInterface::GetDataSafe(DataInterface, DataInterfaceContext, CallResult));
}
/*
FRigUnit_DataInterface_Pose_Execute()
{
	using namespace UE::DataInterface;

	// @TODO: ensure that context arg is always present to avoid these checks here
	if(Context.State == EControlRigState::Update && ExecuteContext.OpaqueArguments.Num() > 1 && ExecuteContext.OpaqueArguments[1] != nullptr)
	{
		const FDataInterfaceUnitContext& DataInterfaceUnitContext = *static_cast<const FDataInterfaceUnitContext*>(ExecuteContext.OpaqueArguments[1]);
		const FContext& DataInterfaceContext = DataInterfaceUnitContext.DataInterfaceContext;

		// Wrap the internal result we are going to be writing to 
		TWrapParam<FDataInterfaceExecuteContext> CallResult(&Result);

		// Call the interface
		DataInterfaceUnitContext.bResult &= UE::DataInterface::GetDataSafe(PoseInterface, DataInterfaceContext, CallResult);
	}
}
*/
FRigUnit_FloatOperator_Execute()
{}

FRigUnit_PoseOperator_Execute()
{}

FRigUnit_DataInterface_SequencePlayer_Execute()
{}

struct FSpringDamperState
{
	float Value = 0.0f;
	float ValueRate = 0.0f;
};

IMPLEMENT_DATA_INTERFACE_STATE_TYPE(FSpringDamperState, SpringDamperState);

FRigUnit_TestFloatState_Execute()
{
	using namespace UE::DataInterface;

	// @TODO: ensure that context arg is always present to avoid these checks here
	const FContext& DataInterfaceContext = ExecuteContext.GetContext();

	const TParam<FSpringDamperState> State = DataInterfaceContext.GetState<FSpringDamperState>(ExecuteContext.GetInterface(), 0);
	const float DeltaTime = DataInterfaceContext.GetDeltaTime();

	FKernel::Run(DataInterfaceContext,
		[DeltaTime, &Result](FSpringDamperState& InOutState,
					float InTargetValue,
					float InTargetValueRate,
					float InSmoothingTime,
					float InDampingRatio)
			{
				FMath::SpringDamperSmoothing(
					InOutState.Value,
					InOutState.ValueRate,
					InTargetValue,
					InTargetValueRate,
					DeltaTime,
					InSmoothingTime,
					InDampingRatio);

				Result = InOutState.Value;
			},
			State, TargetValue, TargetValueRate, SmoothingTime, DampingRatio);
}
