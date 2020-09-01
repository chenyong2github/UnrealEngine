// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/RigVMNode.h"
#include "RigVMBranchNode.generated.h"

/**
 * A branch node is used to branch between two blocks of execution
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMBranchNode : public URigVMNode
{
	GENERATED_BODY()

public:

	// Override from URigVMNode
	virtual FString GetNodeTitle() const override { return BranchName; }
	virtual FLinearColor GetNodeColor() const override { return FLinearColor::Black; }

private:

	static const FString BranchName;
	static const FString ConditionName;
	static const FString TrueName;
	static const FString FalseName;

	friend class URigVMController;
	friend class URigVMCompiler;
	friend class UControlRigBranchNodeSpawner;
};

