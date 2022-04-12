// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeParameterTypes.generated.h"

/**
 * This base class was used to filter the whole family of structs wrapping types supported
 * by StateTree parameters. If we decide to allow any struct then this is not required.
 *		UPROPERTY(..., meta = (BaseStruct = "StateTreeParameterType", ExcludeBaseStruct))
 *	vs	UPROPERTY(...)
 */
USTRUCT(meta = (Hidden))
struct STATETREEMODULE_API FStateTreeParameterType
{
	GENERATED_BODY()
};

USTRUCT(DisplayName = "Bool")
struct STATETREEMODULE_API FStateTreeParameterBool : public FStateTreeParameterType
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bValue = false;
};

USTRUCT(DisplayName = "Int")
struct STATETREEMODULE_API FStateTreeParameterInt : public FStateTreeParameterType
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	int32 Value = 0;
};

USTRUCT(DisplayName = "Float")
struct STATETREEMODULE_API FStateTreeParameterFloat : public FStateTreeParameterType
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	float Value = 0.f;
};

USTRUCT(DisplayName = "Real")
struct STATETREEMODULE_API FStateTreeParameterReal : public FStateTreeParameterType
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	double Value = 0.0;
};

USTRUCT(DisplayName = "Vector")
struct STATETREEMODULE_API FStateTreeParameterVector : public FStateTreeParameterType
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	FVector Value = FVector::ZeroVector;
};

USTRUCT(DisplayName = "Quat")
struct STATETREEMODULE_API FStateTreeParameterQuat : public FStateTreeParameterType
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	FQuat Value = FQuat::Identity;
};

USTRUCT(DisplayName = "Transform")
struct STATETREEMODULE_API FStateTreeParameterTransform : public FStateTreeParameterType
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	FTransform Value = FTransform::Identity;
};

USTRUCT(DisplayName = "String")
struct STATETREEMODULE_API FStateTreeParameterString : public FStateTreeParameterType
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	FString Value;
};

USTRUCT(DisplayName = "Name")
struct STATETREEMODULE_API FStateTreeParameterName : public FStateTreeParameterType
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	FName Value;
};
