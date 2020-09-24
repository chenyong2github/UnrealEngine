// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/RigVMNode.h"
#include "RigVMCore/RigVMStruct.h"
#include "UObject/StructOnScope.h"
#include "RigVMStructNode.generated.h"

/**
 * The Struct Node represents a Function Invocation of a RIGVM_METHOD
 * declared on a USTRUCT. Struct Nodes have input / output pins for all
 * struct UPROPERTY members.
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMStructNode : public URigVMNode
{
	GENERATED_BODY()

public:

	// Override node functions
	virtual FString GetNodeTitle() const override;
	virtual FText GetToolTipText() const override;
	virtual bool IsDefinedAsConstant() const override;
	virtual bool IsDefinedAsVarying() const override;
	virtual FName GetEventName() const override;
	virtual FName GetSliceContextForPin(URigVMPin* InRootPin, const FRigVMUserDataArray& InUserData) override;
	virtual int32 GetNumSlicesForContext(const FName& InContextName, const FRigVMUserDataArray& InUserData) override;

	bool IsDeprecated() const;
	FString GetDeprecatedMetadata() const;

	// Returns the UStruct for this struct node
	// (the struct declaring the RIGVM_METHOD)
	UFUNCTION(BlueprintCallable, Category = RigVMStructNode)
	UScriptStruct* GetScriptStruct() const;

	// return true if this node is a loop node
	bool IsLoopNode() const;

	// Returns the name of the declared RIGVM_METHOD
	UFUNCTION(BlueprintCallable, Category = RigVMStructNode)
	FName GetMethodName() const;

	// Returns the default value for the struct as text
	UFUNCTION(BlueprintCallable, Category = RigVMStructNode)
	FString GetStructDefaultValue() const;

	// Returns an instance of the struct with the current values.
	// @param bUseDefault If set to true the default struct will be created - otherwise the struct will contains the values from the node
	TSharedPtr<FStructOnScope> ConstructStructInstance(bool bUseDefault = false) const;

	static const FName LoopSliceContextName;

protected:

	virtual FText GetToolTipTextForPin(const URigVMPin* InPin) const override;

private:

	UPROPERTY()
	UScriptStruct* ScriptStruct;

	UPROPERTY()
	FName MethodName;

	friend class URigVMController;
};

