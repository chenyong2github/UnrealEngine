// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LiveLinkTypes.h"


#include "LiveLinkTest.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct FLiveLinkInnerTestInternal
{
	GENERATED_BODY()

	UPROPERTY()
	float InnerSingleFloat;

	UPROPERTY()
	int32 InnerSingleInt;

	UPROPERTY()
	FVector InnerVectorDim[2];

	UPROPERTY()
	float InnerFloatDim[2];

	UPROPERTY()
	int32 InnerIntDim[2];

	UPROPERTY()
	TArray<int32> InnerIntArray;
};

USTRUCT()
struct FLiveLinkTestFrameDataInternal : public FLiveLinkBaseFrameData
{
	GENERATED_BODY()

	UPROPERTY()
	float NotInterpolated;

	UPROPERTY(BlueprintReadOnly, Category = "Test", Interp)
	FVector SingleVector;

	UPROPERTY(BlueprintReadOnly, Category = "Test", Interp)
	FLiveLinkInnerTestInternal SingleStruct;

	UPROPERTY(BlueprintReadOnly, Category = "Test", Interp)
	float SingleFloat;

	UPROPERTY(BlueprintReadOnly, Category = "Test", Interp)
	int32 SingleInt;

	UPROPERTY(BlueprintReadOnly, Category = "Test", Interp)
	TArray<FVector> VectorArray;

	UPROPERTY(BlueprintReadOnly, Category = "Test", Interp)
	TArray<FLiveLinkInnerTestInternal> StructArray;

	UPROPERTY(BlueprintReadOnly, Category = "Test", Interp)
	TArray<float> FloatArray;

	UPROPERTY(BlueprintReadOnly, Category = "Test", Interp)
	TArray<int32> IntArray;
};
