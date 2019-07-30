// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StaticMethodStructHeader.generated.h"

USTRUCT()
struct FStaticMethodStructBase
{
	GENERATED_BODY()
		
	UPROPERTY(meta = (Input))
	float Inherited;

	UPROPERTY(meta = (Output))
	float InheritedOutput;
};

USTRUCT()
struct FStaticMethodStruct : public FStaticMethodStructBase
{
	GENERATED_BODY()

	STATIC_VIRTUAL_METHOD()
	void Clear();

	STATIC_VIRTUAL_METHOD()
	virtual void Execute(bool bAdditionalFlag, const FString& InString) override;

	STATIC_VIRTUAL_METHOD()
	void Compute(float TestFloat);

	UPROPERTY(meta = (Input))
	float A;

	UPROPERTY(meta = (Output))
	float B;

	UPROPERTY(meta = (Input))
	FVector C;

	UPROPERTY(meta = (Output))
	FVector D;

	UPROPERTY(meta = (Input, Output))
	TArray<FVector> E;

	UPROPERTY()
	float Cache;
};
