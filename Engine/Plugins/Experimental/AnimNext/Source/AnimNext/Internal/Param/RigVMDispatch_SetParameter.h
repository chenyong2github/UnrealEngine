// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMCore/RigVMDispatchFactory.h"
#include "RigVMDispatch_SetParameter.generated.h"

namespace UE::AnimNext::UncookedOnly
{
struct FUtils;
}

/*
 * Sets a parameter's value
 */
USTRUCT(meta=(DisplayName = "Set Parameter", NodeColor = "0.8, 0, 0.2, 1"))
struct ANIMNEXT_API FRigVMDispatch_SetParameter : public FRigVMDispatchFactory
{
	GENERATED_BODY()

	FRigVMDispatch_SetParameter();

private:
	friend struct UE::AnimNext::UncookedOnly::FUtils;

	virtual FName GetArgumentNameForOperandIndex(int32 InOperandIndex, int32 InTotalOperands) const override;
	virtual TArray<FRigVMTemplateArgument> GetArguments() const override;
	virtual TArray<FRigVMExecuteArgument> GetExecuteArguments_Impl(const FRigVMDispatchContext& InContext) const override;
	virtual FRigVMTemplateTypeMap OnNewArgumentType(const FName& InArgumentName, TRigVMTypeIndex InTypeIndex) const override;
	virtual bool IsSingleton() const override { return true; } 

	virtual FRigVMFunctionPtr GetDispatchFunctionImpl(const FRigVMTemplateTypeMap& InTypes) const override
	{
		return &FRigVMDispatch_SetParameter::Execute;
	}
	static void Execute(FRigVMExtendedExecuteContext& InContext, FRigVMMemoryHandleArray Handles, FRigVMPredicateBranchArray RigVMBranches);

	static const FName ValueName;
	static const FName ExecuteContextName;
};
