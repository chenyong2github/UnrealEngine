// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMStructHeader.generated.h"

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

	UPROPERTY(meta = (Input, Output, MaxArraySize = 8))
	TArray<FVector> E;

	UPROPERTY(meta = (Input))
	TArray<FVector> F;

	UPROPERTY(meta = (Output, MaxArraySize = 8))
	TArray<FVector> G;

	UPROPERTY(meta = (MaxArraySize = 8))
	TArray<FVector> H;

	UPROPERTY()
	float Cache;
};
