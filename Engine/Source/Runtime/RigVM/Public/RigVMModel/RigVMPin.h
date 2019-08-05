// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "RigVMPin.generated.h"

class URigVMGraph;
class URigVMNode;

UENUM()
enum class ERigVMPinDirection : uint8
{
	Input,
	Output,
	IO,
	Invalid
};

UCLASS()
class RIGVM_API URigVMPin : public UObject
{
	GENERATED_BODY()

public:

	URigVMPin();

	FString GetPinPath() const;
	ERigVMPinDirection GetDirection() const;
	bool IsConstant() const;
	int32 IsArray() const;
	int32 GetArrayIndex() const;
	FString GetCPPType() const;

	URigVMPin* GetParentPin() const;
	const TArray<URigVMPin*>& GetSubPins() const;
	const TArray<URigVMPin*>& GetConnectedPins() const;

	URigVMNode* GetNode() const;
	URigVMGraph* GetGraph() const;

private:

	UPROPERTY()
	ERigVMPinDirection Direction;

	UPROPERTY()
	bool bIsConstant;

	UPROPERTY()
	int32 ArrayIndex;

	UPROPERTY()
	FString CPPType;

	UPROPERTY()
	TArray<URigVMPin*> SubPins;

	UPROPERTY()
	TArray<URigVMPin*> ConnectedPins;

	friend class URigVMController;
};

