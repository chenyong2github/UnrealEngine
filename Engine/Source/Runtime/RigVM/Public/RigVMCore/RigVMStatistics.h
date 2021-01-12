// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMStatistics.generated.h"

USTRUCT()
struct FRigVMMemoryStatistics
{
	GENERATED_BODY()

public:

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	uint32 RegisterCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	uint32 DataBytes = 0;

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	uint32 TotalBytes = 0;
};

USTRUCT()
struct FRigVMByteCodeStatistics
{
	GENERATED_BODY()

public:

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	uint32 InstructionCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	uint32 DataBytes = 0;
};

USTRUCT()
struct FRigVMStatistics
{
	GENERATED_BODY()

public:

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	uint32 BytesForCDO = 0;

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	uint32 BytesPerInstance = 0;

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	FRigVMMemoryStatistics LiteralMemory;

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	FRigVMMemoryStatistics WorkMemory;

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	uint32 BytesForCaching = 0;

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	FRigVMByteCodeStatistics ByteCode;
};