// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNode.h"
#include "OptimusDataType.h"
#include "IOptimusValueProvider.h"

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

UCLASS(Hidden)
class UOptimusNode_ConstantValue :
	public UOptimusNode,
	public IOptimusValueProvider
{
	GENERATED_BODY()

public:
	FName GetNodeCategory() const override
	{
		return CategoryName::Values; 
	}

	FOptimusDataTypeRef GetDataType() const;

	// IOptimusValueProvider overrides 
	TArray<uint8> GetShaderValue() const override;

protected:
	void ConstructNode() override;
};
