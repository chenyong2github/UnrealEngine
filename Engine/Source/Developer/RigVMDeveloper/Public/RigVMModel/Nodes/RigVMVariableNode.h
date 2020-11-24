// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMCore/RigVMExternalVariable.h"
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
	UObject* CPPTypeObject = nullptr;

	// The default value of the variable
	UPROPERTY(BlueprintReadOnly, Category = RigVMGraphVariableDescription)
	FString DefaultValue;

	// Returns nullptr external variable matching this description
	FORCEINLINE FRigVMExternalVariable ToExternalVariable() const
	{
		FRigVMExternalVariable ExternalVariable;
		ExternalVariable.Name = Name;

		if (CPPType.StartsWith(TEXT("TArray<")))
		{
			ExternalVariable.bIsArray = true;
			ExternalVariable.TypeName = *CPPType.Mid(7, CPPType.Len() - 8);
			ExternalVariable.TypeObject = CPPTypeObject;
		}
		else
		{
			ExternalVariable.bIsArray = false;
			ExternalVariable.TypeName = *CPPType;
			ExternalVariable.TypeObject = CPPTypeObject;
		}

		ExternalVariable.bIsPublic = false;
		ExternalVariable.bIsReadOnly = false;
		ExternalVariable.Memory = nullptr;
		return ExternalVariable;
	}
};

/**
 * The Variable Node represents a mutable value / local state within the
 * the Function / Graph. Variable Node's can be a getter or a setter.
 * Getters are pure nodes with just an output value pin, while setters
 * are mutable nodes with an execute and input value pin.
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMVariableNode : public URigVMNode
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
	UObject* GetCPPTypeObject() const;

	// Returns the default value of the variable as a string
	UFUNCTION(BlueprintCallable, Category = RigVMVariableNode)
	FString GetDefaultValue() const;

	// Returns this variable node's variable description
	UFUNCTION(BlueprintCallable, Category = RigVMVariableNode)
	FRigVMGraphVariableDescription GetVariableDescription() const;

	// Override of node title
	virtual FLinearColor GetNodeColor() const override { return FLinearColor::Blue; }
	virtual bool IsDefinedAsVarying() const override { return true; }

private:

	static const FString VariableName;
	static const FString ValueName;

	friend class URigVMController;
	friend class URigVMCompiler;
	friend class FRigVMVarExprAST;
	friend class FRigVMParserAST;
};

