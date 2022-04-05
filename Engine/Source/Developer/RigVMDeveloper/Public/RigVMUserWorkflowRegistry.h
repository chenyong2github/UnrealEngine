// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMCore/RigVMUserWorkflow.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMUserWorkflowRegistry.generated.h"

DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(TArray<FRigVMUserWorkflow>, FRigVMUserWorkflowProvider, const UObject*, InSubject);

UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMUserWorkflowRegistry : public UObject
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintPure, Category = FRigVMUserWorkflowRegistry)
   	static URigVMUserWorkflowRegistry* Get();

	UFUNCTION(BlueprintCallable, Category = FRigVMUserWorkflowRegistry)
	int32 RegisterProvider(const UScriptStruct* InStruct, FRigVMUserWorkflowProvider InProvider);

	UFUNCTION(BlueprintCallable, Category = FRigVMUserWorkflowRegistry)
	void UnregisterProvider(int32 InHandle);

	UFUNCTION(BlueprintPure, Category = FRigVMUserWorkflowRegistry)
	TArray<FRigVMUserWorkflow> GetWorkflows(ERigVMUserWorkflowType InType, const UScriptStruct* InStruct, const UObject* InSubject) const;

private:

	int32 MaxHandle = 0;
	mutable TArray<TTuple<int32, const UScriptStruct*, FRigVMUserWorkflowProvider>> Providers;
};
