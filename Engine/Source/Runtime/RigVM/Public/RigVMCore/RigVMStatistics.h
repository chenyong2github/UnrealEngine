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
	uint32 RegisterCount;

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	uint32 DataBytes;

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	uint32 TotalBytes;
};

USTRUCT()
struct FRigVMByteCodeStatistics
{
	GENERATED_BODY()

public:

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	uint32 InstructionCount;

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	uint32 DataBytes;
};

USTRUCT()
struct FRigVMStatistics
{
	GENERATED_BODY()

public:

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	uint32 BytesForCDO;

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	uint32 BytesPerInstance;

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	FRigVMMemoryStatistics LiteralMemory;

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	FRigVMMemoryStatistics WorkMemory;

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	uint32 BytesForCaching;

	UPROPERTY(VisibleAnywhere, Category = "Statistics")
	FRigVMByteCodeStatistics ByteCode;
};