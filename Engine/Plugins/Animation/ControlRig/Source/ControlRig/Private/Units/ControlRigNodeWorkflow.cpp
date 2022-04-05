// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/ControlRigNodeWorkflow.h"
#include "RigVMCore/RigVMStruct.h"

bool UControlRigWorkflowOptions::EnsureAtLeastOneRigElementSelected() const
{
	if(Selection.IsEmpty())
	{
		static constexpr TCHAR SelectAtLeastOneRigElementMessage[] = TEXT("Please select at least one element in the hierarchy!");
		Reportf(EMessageSeverity::Error, SelectAtLeastOneRigElementMessage);
		return false;
	}
	return true;
}

// provides the default workflows for any pin
TArray<FRigVMUserWorkflow> UControlRigTransformWorkflowOptions::ProvideWorkflows(const UObject* InSubject)
{
	TArray<FRigVMUserWorkflow> Workflows;

	if(const URigVMPin* Pin = Cast<URigVMPin>(InSubject))
	{
		if(!Pin->IsArray())
		{
			if(Pin->GetCPPType() == TBaseStructure<FTransform>::Get()->GetStructCPPName())
			{
				Workflows.Emplace(
					TEXT("Set from hierarchy"),
					TEXT("Sets the pin to match the global transform of the selected element in the hierarchy"),
					ERigVMUserWorkflowType::PinContext,
					FRigVMWorkflowGetActionsDelegate::CreateStatic(&UControlRigTransformWorkflowOptions::ProvideTransformWorkflow),
					StaticClass()
				);
			}
		}
	}
	return Workflows;
}

TArray<FRigVMUserWorkflowAction> UControlRigTransformWorkflowOptions::ProvideTransformWorkflow(
	const URigVMUserWorkflowOptions* InOptions)
{
	TArray<FRigVMUserWorkflowAction> Actions;

	if(const UControlRigTransformWorkflowOptions* Options = Cast<UControlRigTransformWorkflowOptions>(InOptions))
	{
		if(Options->EnsureAtLeastOneRigElementSelected())
		{
			const FRigElementKey& Key = Options->Selection[0];
			if(FRigTransformElement* TransformElement = (FRigTransformElement*)Options->Hierarchy->Find<FRigTransformElement>(Key))
			{
				const FTransform Transform = Options->Hierarchy->GetTransform(TransformElement, Options->TransformType);
				
				Actions.Emplace(
					ERigVMUserWorkflowActionType::SetPinDefaultValue,
					InOptions->GetSubject<URigVMPin>(),
					FRigVMStruct::ExportToFullyQualifiedText<FTransform>(Transform)
				);
			}
		}
	}
	
	return Actions;
}
