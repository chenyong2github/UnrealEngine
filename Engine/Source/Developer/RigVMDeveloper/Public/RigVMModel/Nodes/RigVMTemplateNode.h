// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMUnitNode.h"
#include "RigVMModel/Nodes/RigVMUnitNode.h"
#include "RigVMCore/RigVMTemplate.h"
#include "RigVMTemplateNode.generated.h"

/**
 * The Template Node represents an unresolved function.
 * Template nodes can morph into all functions implementing
 * the template's template.
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMTemplateNode : public URigVMUnitNode
{
	GENERATED_BODY()

public:

	// default constructor
	URigVMTemplateNode();

	// URigVMUnitNode interface
	virtual UScriptStruct* GetScriptStruct() const;
	virtual FString GetNodeTitle() const override;
	virtual FName GetMethodName() const;
	virtual  FText GetToolTipText() const override;

	virtual FText GetToolTipTextForPin(const URigVMPin* InPin) const override;

	// Returns the notation of the node
	UFUNCTION(BlueprintCallable, Category = Template)
	FName GetNotation() const;

	// returns true if a pin supports a given type
	bool SupportsType(const URigVMPin* InPin, const FString& InCPPType, FString* OutCPPType = nullptr);

	// returns the index of the resolved RigVM function (or INDEX_NONE)
	TArray<int32> GetResolvedPermutationIndices(FRigVMTemplate::FTypeMap* OutTypes = nullptr) const;

	// returns the resolved functions for the template
	TArray<const FRigVMFunction*> GetResolvedPermutations(FRigVMTemplate::FTypeMap* OutTypes = nullptr) const;

	// returns the template used for this node
	const FRigVMTemplate* GetTemplate() const;

	// returns the resolved function or nullptr if there are still unresolved pins left
	const FRigVMFunction* GetResolvedFunction() const;

	// returns true if the template node is resolved
	bool IsResolved() const;

private:

	void InvalidateCache();

	UPROPERTY()
	FName TemplateNotation;

	UPROPERTY()
	FString ResolvedFunctionName;

	TMap<FString, TPair<bool, FRigVMTemplateArgument::FType>> SupportedTypesCache;

	mutable const FRigVMTemplate* CachedTemplate;
	mutable const FRigVMFunction* CachedFunction;

	friend class URigVMController;
};

