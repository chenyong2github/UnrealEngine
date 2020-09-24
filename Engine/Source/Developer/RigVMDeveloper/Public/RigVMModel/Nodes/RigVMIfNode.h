// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/RigVMNode.h"
#include "RigVMIfNode.generated.h"

/**
 * A if node is used to pick between two values
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMIfNode : public URigVMNode
{
	GENERATED_BODY()

public:

	// Override from URigVMNode
	virtual FString GetNodeTitle() const override { return IfName; }
	virtual FLinearColor GetNodeColor() const override { return FLinearColor::Black; }

private:

	static const FString IfName;
	static const FString ConditionName;
	static const FString TrueName;
	static const FString FalseName;
	static const FString ResultName;

	friend class URigVMController;
	friend class URigVMCompiler;
	friend struct FRigVMAddIfNodeAction;
	friend class UControlRigIfNodeSpawner;
	friend class FRigVMParserAST;
};

