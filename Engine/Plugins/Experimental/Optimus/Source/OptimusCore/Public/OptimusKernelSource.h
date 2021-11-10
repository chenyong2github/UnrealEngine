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
		const FString& InSource,
		const FString& InEntryPoint
		)
	{
		Source = InSource;
		EntryPoint = InEntryPoint;
		Hash = GetTypeHash(InSource);
	}
	
	
	FString GetEntryPoint() const override
	{
		return EntryPoint;
	}
	
	FString GetSource() const override
	{
		return Source;
	}
	
	/** Get a hash of the kernel source code. */
	uint64 GetSourceCodeHash() const override
	{
		return Hash;
	}

private:
	UPROPERTY()
	FString EntryPoint;
	
	UPROPERTY()
	FString Source;

	UPROPERTY()
	uint64 Hash;
};
