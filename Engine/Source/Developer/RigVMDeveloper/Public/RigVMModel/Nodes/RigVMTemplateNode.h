// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/RigVMNode.h"
#include "RigVMCore/RigVMTemplate.h"
#include "RigVMTemplateNode.generated.h"

USTRUCT()
struct RIGVMDEVELOPER_API FRigVMTemplatePreferredType
{
	GENERATED_BODY()
	
	FRigVMTemplatePreferredType()
		: Argument(NAME_None)
		, TypeIndex(INDEX_NONE)
	{}
	
	FRigVMTemplatePreferredType(const FName& InArgument, int32 InTypeIndex)
		: Argument(InArgument)
		, TypeIndex(InTypeIndex)
	{}
	
	void UpdateStringFromIndex();
	void UpdateIndexFromString();

	UPROPERTY()
	FName Argument;

	UPROPERTY()
	int32 TypeIndex;

	UPROPERTY()
	FString TypeString;
};

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

	// UObject interface
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;
	virtual void PostLoad() override;

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
	virtual FName GetNotation() const;

	UFUNCTION(BlueprintCallable, Category = Template)
	virtual bool IsSingleton() const;

	// returns true if a pin supports a given type
	bool SupportsType(const URigVMPin* InPin, int32 InTypeIndex, int32* OutTypeIndex = nullptr);

	// returns true if a pin supports a given type after filtering
	bool FilteredSupportsType(const URigVMPin* InPin, int32 InTypeIndex, int32* OutTypeIndex = nullptr, bool bAllowFloatingPointCasts = true);

	// returns the resolved functions for the template
	TArray<const FRigVMFunction*> GetResolvedPermutations() const;

	// returns the template used for this node
	virtual const FRigVMTemplate* GetTemplate() const;

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

	// returns the indeces of the filtered permutations
	const TArray<int32>& GetFilteredPermutationsIndices() const;

	// returns the filtered types of this pin
	TArray<int32> GetFilteredTypesForPin(URigVMPin* InPin) const;

	// returns true if updating pin filters with InTypes would result in different filters 
	bool PinNeedsFilteredTypesUpdate(URigVMPin* InPin, const TArray<int32>& InTypeIndices);
	bool PinNeedsFilteredTypesUpdate(URigVMPin* InPin, URigVMPin* LinkedPin);

	// updates the filtered permutations given a link or the types for a pin
	bool UpdateFilteredPermutations(URigVMPin* InPin, const TArray<int32>& InTypeIndices);
	bool UpdateFilteredPermutations(URigVMPin* InPin, URigVMPin* LinkedPin);

	// initializes the filtered permutations to all possible permutations
	void InitializeFilteredPermutations();

	// Initializes the filtered permutations and preferred permutation from the types of the pins
	void InitializeFilteredPermutationsFromTypes();

	// Converts the preferred types per pin from index to string
	void ConvertPreferredTypesToString();

	// Converts the preferred types per pin from string to indices
	void ConvertPreferredTypesToTypeIndex();

protected:

	virtual void InvalidateCache() override;
	
	TArray<int32> GetNewFilteredPermutations(URigVMPin* InPin, URigVMPin* LinkedPin);
	TArray<int32> GetNewFilteredPermutations(URigVMPin* InPin, const TArray<int32>& InTypeIndices);

	TArray<int32> FindPermutationsForTypes(const TArray<FRigVMTemplatePreferredType>& ArgumentTypes, bool bAllowCasting = false) const;
	TArray<FRigVMTemplatePreferredType> GetArgumentTypesForPermutation(const int32 InPermutationIndex) const;

	UPROPERTY()
	FName TemplateNotation;

	UPROPERTY()
	FString ResolvedFunctionName;

	// Indicates a preferred permuation using the types of the arguments
	// Each element is in the format "ArgumentName:CPPType"
	UPROPERTY()
	TArray<FString> PreferredPermutationTypes_DEPRECATED;

	UPROPERTY()
	TArray<FRigVMTemplatePreferredType> PreferredPermutationPairs;

	TArray<int32> FilteredPermutations;
	TMap<TPair<FName,int32>, TPair<bool, int32>> SupportedTypesCache;

	mutable const FRigVMTemplate* CachedTemplate;
	mutable const FRigVMFunction* CachedFunction;
	mutable TArray<int32> ResolvedPermutations;

	friend class URigVMController;
	friend class UControlRigBlueprint;
	friend struct FRigVMSetTemplateFilteredPermutationsAction;
	friend struct FRigVMSetPreferredTemplatePermutationsAction;
	friend struct FRigVMRemoveNodeAction;
};

