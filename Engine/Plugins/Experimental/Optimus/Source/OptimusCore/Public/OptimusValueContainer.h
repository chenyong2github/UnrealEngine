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
	// This class should be parented to the asset object, instead of the package
	// because the engine no longer supports multiple 'assets' per package
	DECLARE_WITHIN(UObject)

	static FName ValuePropertyName;
	
	// UClass overrides
	void Link(FArchive& Ar, bool bRelinkExistingProperties) override;
	
	static UClass *GetClassForType(
		UPackage*InPackage,
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

	void PostLoad() override;
	
	static UOptimusValueContainer* MakeValueContainer(UObject* InOwner, FOptimusDataTypeRef InDataTypeRef);

	FOptimusDataTypeRef GetValueType() const;
	TArray<uint8> GetShaderValue() const;
};