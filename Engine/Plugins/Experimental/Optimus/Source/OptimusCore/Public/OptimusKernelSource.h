// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeFramework/ComputeKernelSource.h"

#include "OptimusKernelSource.generated.h"

UCLASS()
class OPTIMUSCORE_API UOptimusKernelSource :
	public UComputeKernelSource
{
	GENERATED_BODY()
public:
	void SetSourceAndEntryPoint(
		const FIntVector& InGroupSize,
		const FString& InSource,
		const FString& InEntryPoint
		)
	{
		GroupSize = InGroupSize;
		Source = InSource;
		EntryPoint = InEntryPoint;
	}
	
	FString GetEntryPoint() const override
	{
		return EntryPoint;
	}

	FIntVector GetGroupSize() const override
	{
		return GroupSize;
	}
	
	FString GetSource() const override
	{
		return Source;
	}
	
private:
	UPROPERTY()
	FString EntryPoint;

	UPROPERTY()
	FIntVector GroupSize = FIntVector(64, 1, 1);
	
	UPROPERTY()
	FString Source;
};
