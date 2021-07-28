// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNode.h"
#include "OptimusDataType.h"

#include "OptimusNode_ConstantValue.generated.h"

struct FOptimusDataTypeRef;

UCLASS()
class UOptimusNode_ConstantValueGeneratorClass :
	public UClass
{
	GENERATED_BODY()

public:
	// UClass overrides
	void Link(FArchive& Ar, bool bRelinkExistingProperties) override;
	
	static UClass *GetClassForType(
		UObject *InPackage,
		FOptimusDataTypeRef InDataType
		);

	UPROPERTY()
	FOptimusDataTypeRef DataType;
};

UCLASS(NotPlaceable)
class UOptimusNode_ConstantValue :
	public UOptimusNode
{
	GENERATED_BODY()

public:
	FName GetNodeCategory() const override
	{
		return CategoryName::Values; 
	}

	FOptimusDataTypeRef GetDataType() const;

	// Returns the stored value as a shader-compatible value.
	TArray<uint8> GetShaderValue() const;

protected:
	void CreatePins() override;
};
