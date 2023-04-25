// Copyright Epic Games, Inc. All Rights Reserved.

#include "Param/RigVMDispatch_SetParameter.h"

#include "Param/ParametersExecuteContext.h"
#include "RigVMCore/RigVMStruct.h"
#include "RigVMCore/RigVM.h"

class UAnimNextParameter;
const FName FRigVMDispatch_SetParameter::ValueName = TEXT("Value");
const FName FRigVMDispatch_SetParameter::ExecuteContextName = TEXT("ExecuteContext");

FRigVMDispatch_SetParameter::FRigVMDispatch_SetParameter()
{
	FactoryScriptStruct = StaticStruct();
}

FName FRigVMDispatch_SetParameter::GetArgumentNameForOperandIndex(int32 InOperandIndex, int32 InTotalOperands) const
{
	static const FName ArgumentNames[] = 
	{
		ValueName
	};
	check(InTotalOperands == UE_ARRAY_COUNT(ArgumentNames));
	return ArgumentNames[InOperandIndex];
}

TArray<FRigVMTemplateArgument> FRigVMDispatch_SetParameter::GetArguments() const
{
	const TArray<FRigVMTemplateArgument::ETypeCategory> ValueCategories =
	{
		FRigVMTemplateArgument::ETypeCategory_SingleAnyValue,
		FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue
	};
	return { FRigVMTemplateArgument(ValueName, ERigVMPinDirection::Input, ValueCategories) };
}

TArray<FRigVMExecuteArgument> FRigVMDispatch_SetParameter::GetExecuteArguments_Impl(const FRigVMDispatchContext& InContext) const
{
	return { { ExecuteContextName, ERigVMPinDirection::Input } };
}

FRigVMTemplateTypeMap FRigVMDispatch_SetParameter::OnNewArgumentType(const FName& InArgumentName, TRigVMTypeIndex InTypeIndex) const
{
	FRigVMTemplateTypeMap Types;
	Types.Add(ValueName, InTypeIndex);
	return Types;
}

void FRigVMDispatch_SetParameter::Execute(FRigVMExtendedExecuteContext& InContext, FRigVMMemoryHandleArray Handles)
{
	const FProperty* ValueProperty = Handles[0].GetResolvedProperty(); 
	check(ValueProperty);
	const uint8* SourceData =  Handles[0].GetData();

	FAnimNextParametersExecuteContext& Context = InContext.GetPublicData<FAnimNextParametersExecuteContext>();
	TArrayView<uint8> TargetData = Context.GetData();

	ValueProperty->CopyCompleteValue(TargetData.GetData(), SourceData);
}

