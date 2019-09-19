// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "RigVMPin.h"
#include "RigVMLink.generated.h"

class URigVMGraph;

/**
 * The Link represents a connection between two Pins
 * within a Graph. The Link can be accessed on the 
 * Graph itself - or through the URigVMPin::GetLinks()
 * method.
 */
UCLASS(BlueprintType)
class RIGVMEDITOR_API URigVMLink : public UObject
{
	GENERATED_BODY()

public:

	// Default constructor
	URigVMLink()
	{
		SourcePin = TargetPin = nullptr;
	}

	// Serialization override
	virtual void Serialize(FArchive& Ar) override;

	// Returns the current index of this Link within its owning Graph.
	UFUNCTION(BlueprintCallable, Category = RigVMLink)
	int32 GetLinkIndex() const;

	// Returns the Link's owning Graph/
	UFUNCTION(BlueprintCallable, Category = RigVMLink)
	URigVMGraph* GetGraph() const;

	// Returns the source Pin of this Link (or nullptr)
	UFUNCTION(BlueprintCallable, Category = RigVMLink)
	URigVMPin* GetSourcePin();

	// Returns the target Pin of this Link (or nullptr)
	UFUNCTION(BlueprintCallable, Category = RigVMLink)
	URigVMPin* GetTargetPin();

	// Returns a string representation of the Link,
	// for example: "NodeA.Color.R -> NodeB.Translation.X"
	UFUNCTION(BlueprintCallable, Category = RigVMLink)
	FString GetPinPathRepresentation();

private:

	FString SourcePinPath;
	FString TargetPinPath;

	URigVMPin* SourcePin;
	URigVMPin* TargetPin;

	friend class URigVMController;
};

