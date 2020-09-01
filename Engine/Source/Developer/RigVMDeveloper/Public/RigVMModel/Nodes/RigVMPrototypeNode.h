// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/RigVMNode.h"
#include "RigVMCore/RigVMPrototype.h"
#include "RigVMPrototypeNode.generated.h"

/**
 * The Prototype Node represents an unresolved function.
 * Prototype nodes can morph into all functions implementing
 * the prototype.
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMPrototypeNode : public URigVMNode
{
	GENERATED_BODY()

public:

	// default constructor
	URigVMPrototypeNode();

	virtual FText GetToolTipTextForPin(const URigVMPin* InPin) const override;

	// Returns the notation of the node
	UFUNCTION(BlueprintCallable, Category = RigVMPrototypeNode)
	FName GetNotation() const;

	// returns true if a pin supports a given type
	bool SupportsType(const URigVMPin* InPin, const FString& InCPPType);

	// returns the index of the resolved RigVM function (or INDEX_NONE)
	int32 GetResolvedFunctionIndex(FRigVMPrototype::FTypeMap* OutTypes = nullptr);

private:

	const FRigVMPrototype* GetPrototype() const;

	UPROPERTY()
	FName PrototypeNotation;

	UPROPERTY(transient)
	TMap<FString, bool> SupportedTypesCache;

	mutable const FRigVMPrototype* CachedPrototype;

	friend class URigVMController;
};

