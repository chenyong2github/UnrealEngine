// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMStructHeader.generated.h"

UENUM()
enum class ERigVMTestEnum : uint8
{
	A,
	B,
	C
};

UENUM()
namespace ERigVMTestNameSpaceEnum
{
	enum Type
	{
		A,
		B,
		C
	};
}

USTRUCT()
struct FRigVMStructBase
{
	GENERATED_BODY()
		
	UPROPERTY(meta = (Input))
	float Inherited;

	UPROPERTY(meta = (Output))
	float InheritedOutput;
};

USTRUCT()
struct FRigVMMethodStruct : public FRigVMStructBase
{
	GENERATED_BODY()

	RIGVM_METHOD()
	void Clear();

	RIGVM_METHOD()
	virtual void Execute(bool bAdditionalFlag = false, const FString& InString = TEXT("")) override;

	RIGVM_METHOD()
	void Compute(float TestFloat);

	UPROPERTY(meta = (Input))
	float A;

	UPROPERTY(meta = (Output))
	float B;

	UPROPERTY(meta = (Input))
	FVector C;

	UPROPERTY(meta = (Output))
	FVector D;

	UPROPERTY(meta = (Input, Output, ArraySize = 8))
	TArray<FVector> E;

	UPROPERTY(meta = (Input))
	TArray<FVector> F;

	UPROPERTY(meta = (Output, ArraySize = 8))
	TArray<FVector> G;

	UPROPERTY(meta = (ArraySize = 8))
	TArray<FVector> H;

	UPROPERTY()
	TArray<FVector> I;

	UPROPERTY()
	TArray<float> J;

	UPROPERTY()
	float Cache;

	UPROPERTY(meta = (Input))
	TEnumAsByte<ERigVMTestEnum> InputEnum;

	UPROPERTY()
	TEnumAsByte<ERigVMTestEnum> HiddenEnum;

	UPROPERTY(meta = (Input))
	TEnumAsByte<ERigVMTestNameSpaceEnum::Type> InputNameSpaceEnum;

	UPROPERTY()
	TEnumAsByte<ERigVMTestNameSpaceEnum::Type> HiddenNameSpaceEnum;
};
