// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StructUtilsTestTypes.generated.h"

USTRUCT()
struct FTestStructSimple
{
	GENERATED_BODY()
	
	FTestStructSimple() = default;
	FTestStructSimple(const float InFloat) : Float(InFloat) {}
	
	UPROPERTY()
	float Float = 0.0f;
};

USTRUCT()
struct FTestStructComplex
{
	GENERATED_BODY()
	
	FTestStructComplex() = default;
	FTestStructComplex(const FString& InString) : String(InString) {}
	
	UPROPERTY()
	FString String; 

	UPROPERTY()
	TArray<FString> StringArray;
};


USTRUCT()
struct FTestStructSimple1
{
	GENERATED_BODY()
	
	FTestStructSimple1() = default;
	
	UPROPERTY()
	float Float = 0.0f;
};

USTRUCT()
struct FTestStructSimple2
{
	GENERATED_BODY()
	
	FTestStructSimple2() = default;
	
	UPROPERTY()
	float Float = 0.0f;
};

USTRUCT()
struct FTestStructSimple3
{
	GENERATED_BODY()
	
	FTestStructSimple3() = default;
	
	UPROPERTY()
	float Float = 0.0f;
};

USTRUCT()
struct FTestStructSimple4
{
	GENERATED_BODY()
	
	FTestStructSimple4() = default;
	
	UPROPERTY()
	float Float = 0.0f;
};

USTRUCT()
struct FTestStructSimple5
{
	GENERATED_BODY()
	
	FTestStructSimple5() = default;
	
	UPROPERTY()
	float Float = 0.0f;
};

USTRUCT()
struct FTestStructSimple6
{
	GENERATED_BODY()
	
	FTestStructSimple6() = default;
	
	UPROPERTY()
	float Float = 0.0f;
};

USTRUCT()
struct FTestStructSimple7
{
	GENERATED_BODY()
	
	FTestStructSimple7() = default;
	
	UPROPERTY()
	float Float = 0.0f;
};
