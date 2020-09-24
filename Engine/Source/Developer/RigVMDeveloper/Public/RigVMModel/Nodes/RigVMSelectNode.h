// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/RigVMNode.h"
#include "RigVMSelectNode.generated.h"

/**
 * A select node is used to select between multiple values
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMSelectNode : public URigVMNode
{
	GENERATED_BODY()

public:

	// Override from URigVMNode
	virtual FString GetNodeTitle() const override { return SelectName; }
	virtual FLinearColor GetNodeColor() const override { return FLinearColor::Black; }

protected:

	virtual bool AllowsLinksOn(const URigVMPin* InPin) const override;

private:

	static const FString SelectName;
	static const FString IndexName;
	static const FString ValueName;
	static const FString ResultName;

	friend class URigVMController;
	friend class URigVMCompiler;
	friend struct FRigVMAddSelectNodeAction;
	friend class UControlRigSelectNodeSpawner;
	friend class FRigVMParserAST;
	friend class FRigVMSelectExprAST;
};

