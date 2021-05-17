// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusDataType.h"

#include "CoreMinimal.h"
#include "UObject/NameTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "OptimusVariableDescription.generated.h"

USTRUCT()
struct FOptimusVariableMetaDataEntry
{
	GENERATED_BODY()

	FOptimusVariableMetaDataEntry() {}
	FOptimusVariableMetaDataEntry(FName InKey, FString&& InValue)
	    : Key(InKey), Value(MoveTemp(InValue))
	{}

	/** Name of metadata key */
	UPROPERTY(EditAnywhere, Category = VariableMetaDataEntry)
	FName Key;

	/** Name of metadata value */
	UPROPERTY(EditAnywhere, Category = VariableMetaDataEntry)
	FString Value;
};


UCLASS()
class UOptimusVariableDescription : 
	public UObject
{
	GENERATED_BODY()
public:
	UOptimusVariableDescription() = default;

	/** An identifier that uniquely identifies this variable */
	UPROPERTY()
	FGuid Guid;

	/** Name of the variable */
	UPROPERTY(EditAnywhere, Category = VariableDefinition)
	FName VariableName;

	UPROPERTY(EditAnywhere, Category = VariableDefinition, meta=(UseInVariable))
	FOptimusDataTypeRef DataType;

	UPROPERTY(EditAnywhere, Category = VariableDefinition)
	FString FriendlyName;

	UPROPERTY(EditAnywhere, Category = VariableDefinition)
	FText Category;

#if defined(WITH_EDITOR)
	void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
