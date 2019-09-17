// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/RigVMNode.h"
#include "RigVMVariableNode.generated.h"

/**
 * The variable description is used to convey information
 * about unique variables within a Graph. Multiple Variable
 * Nodes can share the same variable description.
 */
USTRUCT(BlueprintType)
struct FRigVMGraphVariableDescription
{
	GENERATED_BODY()

public:

	// comparison operator
	bool operator ==(const FRigVMGraphVariableDescription& Other) const
	{
		return Name == Other.Name;
	}

	// The name of the variable
	UPROPERTY(BlueprintReadOnly, Category = RigVMGraphVariableDescription)
	FName Name;

	// The C++ data type of the variable
	UPROPERTY(BlueprintReadOnly, Category = RigVMGraphVariableDescription)
	FString CPPType;

	// The Struct of the C++ data type of the variable (or nullptr)
	UPROPERTY(BlueprintReadOnly, Category = RigVMGraphVariableDescription)
	UScriptStruct* ScriptStruct;

	// The default value of the variable
	UPROPERTY(BlueprintReadOnly, Category = RigVMGraphVariableDescription)
	FString DefaultValue;
};

/**
 * The Variable Node represents a mutable value / local state within the
 * the Function / Graph. Variable Node's can be a getter or a setter.
 * Getters are pure nodes with just an output value pin, while setters
 * are mutable nodes with an execute and input value pin.
 */
UCLASS(BlueprintType)
class RIGVMEDITOR_API URigVMVariableNode : public URigVMNode
{
	GENERATED_BODY()

public:

	// Default constructor
	URigVMVariableNode();

	// Override of node title
	virtual FString GetNodeTitle() const;

	// Returns the name of the variable
	UFUNCTION(BlueprintCallable, Category = RigVMVariableNode)
	FName GetVariableName() const;

	// Returns true if this node is a variable getter
	UFUNCTION(BlueprintCallable, Category = RigVMVariableNode)
	bool IsGetter() const;

	// Returns the C++ data type of the variable
	UFUNCTION(BlueprintCallable, Category = RigVMVariableNode)
	FString GetCPPType() const;

	// Returns the C++ data type struct of the variable (or nullptr)
	UFUNCTION(BlueprintCallable, Category = RigVMVariableNode)
	UScriptStruct* GetScriptStruct() const;

	// Returns the default value of the variable as a string
	UFUNCTION(BlueprintCallable, Category = RigVMVariableNode)
	FString GetDefaultValue() const;

	// Returns this variable node's variable description
	UFUNCTION(BlueprintCallable, Category = RigVMVariableNode)
	FRigVMGraphVariableDescription GetVariableDescription() const;

	// Override of node title
	virtual FLinearColor GetNodeColor() const override { return FLinearColor::Blue; }

private:

	static const FString ValueName;

	UPROPERTY()
	FName VariableName;

	friend class URigVMController;
};

