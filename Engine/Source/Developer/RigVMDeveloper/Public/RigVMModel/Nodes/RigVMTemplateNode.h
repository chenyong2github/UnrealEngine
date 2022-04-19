// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/RigVMNode.h"
#include "RigVMCore/RigVMTemplate.h"
#include "RigVMTemplateNode.generated.h"

/**
 * The Template Node represents an unresolved function.
 * Template nodes can morph into all functions implementing
 * the template's template.
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMTemplateNode : public URigVMNode
{
	GENERATED_BODY()

public:

	// default constructor
	URigVMTemplateNode();

	// URigVMNode interface
	virtual FString GetNodeTitle() const override;
	virtual FName GetMethodName() const;
	virtual  FText GetToolTipText() const override;
	virtual FText GetToolTipTextForPin(const URigVMPin* InPin) const override;

	// Returns the UStruct for this unit node
	// (the struct declaring the RIGVM_METHOD)
	UFUNCTION(BlueprintCallable, Category = RigVMUnitNode)
	virtual UScriptStruct* GetScriptStruct() const;

	// Returns the notation of the node
	UFUNCTION(BlueprintPure, Category = Template)
	FName GetNotation() const;

	// returns true if a pin supports a given type
	bool SupportsType(const URigVMPin* InPin, const FString& InCPPType, FString* OutCPPType = nullptr);

	// resolves a pin to a new type outputs the new type map
	bool GetTypeMapForNewPinType(const URigVMPin* InPin, const FString& InCPPType, UObject* InCPPTypeObject, FRigVMTemplate::FTypeMap& OutTypes) const;

	// returns the index of the resolved RigVM function (or INDEX_NONE)
	const TArray<int32>& GetResolvedPermutationIndices(FRigVMTemplate::FTypeMap* OutTypes = nullptr) const;

	// returns the resolved functions for the template
	TArray<const FRigVMFunction*> GetResolvedPermutations(FRigVMTemplate::FTypeMap* OutTypes = nullptr) const;

	// returns the template used for this node
	const FRigVMTemplate* GetTemplate() const;

	// returns the type map for the currently resolved pins
	FRigVMTemplate::FTypeMap GetResolvedTypes() const;

	// returns the resolved function or nullptr if there are still unresolved pins left
	const FRigVMFunction* GetResolvedFunction() const;

	// returns true if the template node is resolved
	UFUNCTION(BlueprintPure, Category = Template)
	bool IsResolved() const;

	// returns true if the template is fully unresolved
	UFUNCTION(BlueprintPure, Category = Template)
	bool IsFullyUnresolved() const;

	// returns a default value for pin if it is known
	FString GetInitialDefaultValueForPin(const FName& InRootPinName, const TArray<int32>& InPermutationIndices = TArray<int32>()) const;

	// returns the display name for a pin
	FName GetDisplayNameForPin(const FName& InRootPinName, const TArray<int32>& InPermutationIndices = TArray<int32>()) const;

protected:

	void InvalidateCache();

	UPROPERTY()
	FName TemplateNotation;

	UPROPERTY()
	FString ResolvedFunctionName;

	TMap<FString, TPair<bool, FRigVMTemplateArgument::FType>> SupportedTypesCache;

	mutable const FRigVMTemplate* CachedTemplate;
	mutable const FRigVMFunction* CachedFunction;
	mutable TArray<int32> ResolvedPermutations;

	friend class URigVMController;
};

