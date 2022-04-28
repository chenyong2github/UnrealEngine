// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OptimusDataType.h"
#include "OptimusValueContainer.generated.h"

UCLASS()
class OPTIMUSCORE_API UOptimusValueContainerGeneratorClass : public UClass
{
public:
	GENERATED_BODY()
	
	static FName ValuePropertyName;
	
	// UClass overrides
	void Link(FArchive& Ar, bool bRelinkExistingProperties) override;
	
	static UClass *GetClassForType(
		UObject *InPackage,
		FOptimusDataTypeRef InDataType
		);

	UPROPERTY()
	FOptimusDataTypeRef DataType;
};

UCLASS()
class OPTIMUSCORE_API UOptimusValueContainer : public UObject
{
public:
	GENERATED_BODY()

	static UOptimusValueContainer* MakeValueContainer(UObject* InOwner, FOptimusDataTypeRef InDataTypeRef);

	FOptimusDataTypeRef GetValueType() const;
	TArray<uint8> GetShaderValue() const;
};