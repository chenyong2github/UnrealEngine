// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/RigVMNode.h"
#include "RigVMParameterNode.generated.h"

/**
 * The parameter description is used to convey information
 * about unique parameters within a Graph. Multiple Parameter
 * Nodes can share the same parameter description.
 */
USTRUCT(BlueprintType)
struct FRigVMGraphParameterDescription
{
	GENERATED_BODY()

public:

	// comparison operator
	bool operator ==(const FRigVMGraphParameterDescription& Other) const
	{
		return Name == Other.Name;
	}

	// The name of the parameter
	UPROPERTY(BlueprintReadOnly, Category = RigVMGraphParameterDescription)
	FName Name;

	// True if the parameter is an input
	UPROPERTY(BlueprintReadOnly, Category = RigVMGraphParameterDescription)
	bool bIsInput;

	// The C++ data type of the parameter
	UPROPERTY(BlueprintReadOnly, Category = RigVMGraphParameterDescription)
	FString CPPType;

	// The Struct of the C++ data type of the parameter (or nullptr)
	UPROPERTY(BlueprintReadOnly, Category = RigVMGraphParameterDescription)
	UScriptStruct* ScriptStruct;

	// The default value of the parameter
	UPROPERTY(BlueprintReadOnly, Category = RigVMGraphParameterDescription)
	FString DefaultValue;
};

/**
 * The Parameter Node represents an input or output argument / parameter
 * of the Function / Graph. Parameter Node have only a single value pin.
 */
UCLASS(BlueprintType)
class RIGVMEDITOR_API URigVMParameterNode : public URigVMNode
{
	GENERATED_BODY()

public:

	// Default constructor
	URigVMParameterNode();

	// Override of node title
	virtual FString GetNodeTitle() const;

	// Returns the name of the parameter
	UFUNCTION(BlueprintCallable, Category = RigVMParameterNode)
	FName GetParameterName() const;

	// Returns true if this node is an input
	UFUNCTION(BlueprintCallable, Category = RigVMParameterNode)
	bool IsInput() const;

	// Returns the C++ data type of the parameter
	UFUNCTION(BlueprintCallable, Category = RigVMParameterNode)
	FString GetCPPType() const;

	// Returns the C++ data type struct of the parameter (or nullptr)
	UFUNCTION(BlueprintCallable, Category = RigVMParameterNode)
	UScriptStruct* GetScriptStruct() const;

	// Returns the default value of the parameter as a string
	UFUNCTION(BlueprintCallable, Category = RigVMParameterNode)
	FString GetDefaultValue() const;

	// Returns this parameter node's parameter description
	UFUNCTION(BlueprintCallable, Category = RigVMParameterNode)
	FRigVMGraphParameterDescription GetParameterDescription() const;

	// Override of node title
	virtual FLinearColor GetNodeColor() const override { return FLinearColor::Green; }


private:

	virtual bool ContributesToResult() const override { return !IsInput(); }

	static const FString ValueName;

	UPROPERTY()
	FName ParameterName;

	friend class URigVMController;
};

